# IPC Framework mcumgr Demo

Zephyr-Demo für `actor_framework_cxx` auf dem BBC micro:bit v2. Die Geräte-API läuft über den standardisierten **SMP-over-BLE**-Service von mcumgr; es gibt keinen eigenen GATT-Service, kein Protobuf und kein selbst implementiertes BLE-Framing.

## Architektur

```text
SMP-over-BLE / mcumgr custom group 64
  → CBOR dekodieren
  → AppMgmtRequestEvent publizieren
  → echo_actor | ping_actor | hmi_actor
  → Transaktion abschließen
  → CBOR-SMP-Response
```

Der mcumgr-Handler serialisiert Anfragen, weil die IPC-Transaktion bis zur Antwort eines Actors lebt. Die Feature-Actors bleiben voneinander entkoppelt und bearbeiten jeweils nur ihren Command.

## API

Custom mcumgr group: **64**. Alle Commands sind SMP `write`-Operationen und verwenden CBOR-Maps.

| ID | Command | Request | Response | Actor |
|---:|---|---|---|---|
| 0 | Echo | `{ 1: <bytes> }` | `{ 1: <bytes> }` | `echo_actor` |
| 1 | Ping | `{ 1: <uint> }` | `{ 1: <uint> }` | `ping_actor` |
| 2 | Set matrix symbol | `{ 1: <uint> }` | `{ 1: <uint> }` | `hmi_actor` |

Matrix-Symbole: `1=OFF`, `2=SMILE`, `3=HEART`, `4=CHECK`, `5=CROSS`.

SMP übernimmt GATT Write/Notify, Header, MTU-Fragmentierung und Reassembly. Das Gerät bewirbt den offiziellen SMP-BLE-Service-UUID.

## Python-Clients

`clients/data.py` enthält alle Commands sowie Request-/Response-Typen. Der gemeinsame SMP-over-BLE-Transport liegt in `clients/smp_client.py`.

```sh
source .venv/bin/activate
python clients/echo_client.py --name 'klab BLE' --payload hello
python clients/ping_client.py --name 'klab BLE' --sequence 42
python clients/symbol_client.py --name 'klab BLE' --symbol heart
```

## Build

```sh
mise trust
mise install
mise run setup
mise run update
mise run build
mise run flash
```
