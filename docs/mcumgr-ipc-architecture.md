# mcumgr und IPC-Actors

Diese Anwendung verwendet mcumgr als standardisierten SMP-over-BLE-Transport und das
IPC-Framework für die fachliche Verarbeitung der Commands. mcumgr ersetzt damit nur
den bisherigen Protobuf-/GATT-Transport; die Arbeit bleibt bei den Actors.

```text
Python-Client
  │ SMP-Header + CBOR-Map, GATT Write
  ▼
Zephyr SMP-over-BLE-Transport
  │ Fragmentierung/Reassembly, Request/Response
  ▼
Custom mcumgr group 64 (`mcumgr_app.c`)
  │ AppMgmtRequestEvent
  ▼
echo_actor / ping_actor / hmi_actor
  │ app_mgmt_respond()
  ▼
mcumgr-Handler kodiert CBOR-Response
  │ GATT Notify
  ▼
Python-Client
```

## SMP und Bluetooth

Zephyr stellt mit `CONFIG_MCUMGR_TRANSPORT_BT=y` den offiziellen SMP-GATT-Service
bereit. Dieser hat eine Characteristic für beide Richtungen:

- Client schreibt SMP-Requests per GATT Write.
- Das Gerät sendet SMP-Responses per GATT Notification.

Der Transport übernimmt SMP-Header, ATT-MTU-Fragmentierung und Reassembly. Es gibt
kein eigenes `BleChunk`-Format, keine separaten RX/TX-Characteristics und kein
manuelles Chunking mehr.

`src/smp_ble.c` aktiviert Bluetooth und bewirbt die SMP-Service-UUID. Nach einem
Disconnect wird erst im `recycled`-Callback erneut Advertising gestartet. Zu diesem
Zeitpunkt hat der Bluetooth-Host die Connection-Ressourcen freigegeben.

## Die Custom-mcumgr-Group

`src/mcumgr_app.c` registriert die Group-ID `64`. Die Group enthält drei
Write-Commands:

| Command-ID | CBOR-Request | CBOR-Response | Zuständiger Actor |
| ---: | --- | --- | --- |
| 0: Echo | `{ 1: <bytes> }` | `{ 1: <bytes> }` | `echo_actor` |
| 1: Ping | `{ 1: <uint> }` | `{ 1: <uint> }` | `ping_actor` |
| 2: Set matrix symbol | `{ 1: <uint> }` | `{ 1: <uint> }` | `hmi_actor` |

mcumgr ruft einen Handler synchron auf: Er erwartet, dass dieser die Response-CBOR-Map
fertig kodiert zurückgibt. Actors arbeiten dagegen asynchron über Mailboxes. Die
`app_mgmt_transaction` überbrückt diese beiden Modelle.

## IPC-Request/Response-Transaktion

### Warum sie nötig ist

mcumgr erwartet eine **synchrone** Funktion: Der Handler muss die Response kodiert
haben, bevor er zurückkehrt. `ipc_publish()` arbeitet dagegen **asynchron**: Es kopiert
das Event in die Mailboxes der abonnierten Actors und kehrt zurück, bevor ein Actor den
Command verarbeitet hat.

`app_mgmt_transaction` ist die kleine Brücke zwischen beiden Modellen. Sie ist kein
weiteres IPC-Event und keine globale Response-Queue, sondern der gemeinsame Speicher
für **genau einen** Request und dessen Response:

```c
struct app_mgmt_transaction {
    struct k_sem completed;  /* „Response fertig“ */
    int status;              /* 0 oder negativer errno-Fehler des Actors */
    uint32_t value;          /* z. B. Ping-Sequenz oder Matrix-Symbol */
    uint16_t data_len;
    uint8_t data[256];       /* z. B. Echo-Bytes */
};
```

Der mcumgr-Handler erzeugt diese Struktur auf seinem Stack. Er legt anschließend nur
einen **Zeiger** darauf in `AppMgmtRequestEvent` ab. Das Event selbst enthält weiter
Command, Wert und Request-Daten; die Transaktion ist ausschließlich der Rückkanal.

### Lebenszyklus

1. Der mcumgr-Handler dekodiert die CBOR-Map und erzeugt `transaction`.
2. Er initialisiert `transaction.completed` mit Zähler `0`: Es gibt noch keine Antwort.
3. Er publiziert `AppMgmtRequestEvent`. Alle Feature-Actors können es sehen.
4. Nur der Actor, dessen Command übereinstimmt, verarbeitet das Event, etwa
   `echo_actor` für `APP_MGMT_CMD_ECHO`.
5. Dieser Actor ruft `app_mgmt_respond(transaction, ...)` auf. Die Funktion schreibt
   die Antwort in die Struktur und ruft abschließend `k_sem_give()` auf.
6. Der wartende mcumgr-Handler wird durch `k_sem_take()` freigegeben, liest Status und
   Response-Daten und kodiert daraus die CBOR-SMP-Response.
7. Erst danach kehrt der Handler zurück; damit endet die Lebensdauer der Stack-Struktur.

```text
mcumgr-Workqueue              echo_actor
──────────────────            ───────────────────────
transaction auf Stack anlegen
ipc_publish(event mit &transaction)
k_sem_take(completed)   ───►  Event aus Mailbox lesen
                            app_mgmt_respond(&transaction, ...)
                            k_sem_give(completed)
weiterlaufen            ◄───
CBOR-Response kodieren
Handler kehrt zurück; transaction ist nicht mehr gültig
```

### Warum der Zeiger gültig bleibt

Ein Stack-Zeiger in einem asynchronen Event wäre normalerweise gefährlich: Der Sender
könnte zurückkehren, während ein Actor den Zeiger noch nutzt. Hier verhindert
`k_sem_take(&transaction.completed, K_FOREVER)` genau das. Der Handler kehrt erst
zurück, nachdem der zuständige Actor seine Antwort abgeschlossen hat.

`app_mgmt_request_lock` serialisiert zusätzlich die mcumgr-Handler. Somit gibt es nur
eine offene Stack-basierte Transaktion. Das verhindert parallele SMP-Requests, die
ansonsten gleichzeitig auf Actor-Antworten warten oder einen nicht eindeutig
zuordenbaren Response-Pfad erzeugen könnten.

**Folge:** Jeder unterstützte Command muss garantiert genau einmal
`app_mgmt_respond()` aufrufen. Tut er das nicht (z. B. ein neuer Command ohne
zuständigen Actor), wartet der mcumgr-Handler dauerhaft. Für spätere Commands mit
potenziell langer Verarbeitung sollte deshalb ein Timeout und ein langlebiger
Transaktions-/Request-Pool ergänzt werden.

## CBOR-Konfiguration

Ein mcumgr-Command mit einer CBOR-Map benötigt mindestens eine CBOR-Dekodier- und
Kodier-Ebene. `app/Kconfig` selektiert deshalb:

```text
MCUMGR_SMP_CBOR_MIN_DECODING_LEVEL_1
MCUMGR_SMP_CBOR_MIN_ENCODING_LEVEL_1
```

Ohne diese Selektoren erstellt mcumgr keinen ausreichenden zcbor-Backup-State. Dann
schlägt bereits das Dekodieren einer gültigen Request-Map fehl.

## CDDL und zcbor-Codegenerierung

[`protocol/app_mcumgr.cddl`](../protocol/app_mcumgr.cddl) ist der versionierte
Wire-Vertrag. Daraus erzeugt CMake mit `zcbor` die C-Typen sowie CBOR-Encoder und
-Decoder unter `build/.../generated/`. Diese Dateien werden nicht eingecheckt.
`mcumgr_app.c` verwendet die generierten Funktionen; es enthält keine
handgeschriebenen CBOR-Parser mehr.

## Python-Seite

- `clients/data.py` enthält Group-ID, Command-IDs, UUIDs und die Request-/Response-
  Dataclasses.
- `clients/smp_client.py` erstellt den acht Byte langen SMP-Header, kodiert/decodiert
  CBOR mit `cbor2` und überträgt den Frame über die SMP-Characteristic.
- `echo_client.py`, `ping_client.py` und `symbol_client.py` sind kleine konkrete
  Clients auf Basis des gemeinsamen Transports.

Beispiel:

```sh
mise run test-integration echo
```

Die UART-Ausgabe lässt sich parallel öffnen:

```sh
mise run serial /dev/cu.usbmodemXXXX
```
