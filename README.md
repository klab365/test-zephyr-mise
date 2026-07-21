# IPC Framework BLE Demo

A demo Zephyr application for the [actor_framework_cxx](https://github.com/klab365/actor_framework_cxx) IPC actor framework. It runs on a BBC micro:bit v2 (`nRF52833`) and exposes protobuf requests over a custom BLE GATT service.

The project uses actor_framework_cxx **V1.2.1**, Zephyr via nRF Connect SDK v3.4.0, nanopb for embedded protobuf encoding, and `mise` for tool and task management.

## What it demonstrates

- File-local actors and typed IPC messages.
- Event fan-out with a shared protobuf `RequestEnvelope` / `ResponseEnvelope`.
- BLE GATT writes posted into an actor mailbox, followed by chunk reassembly and protobuf decoding.
- Feature actors that handle only the envelope payloads they own.
- BLE responses chunked into GATT notifications.
- A micro:bit HMI actor that displays protobuf-selected LED-matrix symbols.

```text
BLE client
  │ GATT write (BleChunk)
  ▼
ble_actor ──publishes AppRequestEvent──► echo_actor
  │                                      ping_actor
  │                                      hmi_actor
  ◄──publishes AppResponseEvent────────── selected feature actor
  │ GATT notification (BleChunk)
  ▼
BLE client
```

`AppRequestEvent` is deliberately an event: several feature actors can subscribe and inspect the protobuf `oneof` payload. Commands are used for one-owner operations such as the BLE actor's advertising restart.

## Supported requests

| Protobuf request | Actor | Response |
| --- | --- | --- |
| `EchoRequest` | `echo_actor` | `EchoResponse` with the same payload |
| `PingRequest` | `ping_actor` | `PingResponse` with the same sequence |
| `SetMatrixSymbolRequest` | `hmi_actor` | `SetMatrixSymbolResponse` |

`MatrixSymbol` supports `OFF`, `SMILE`, `HEART`, `CHECK`, and `CROSS`. The micro:bit v2 LED matrix is monochrome red.

Schemas are in [`app/assets/proto/`](app/assets/proto/).

## Prerequisites

- [mise](https://mise.jdx.dev/)
- A BBC micro:bit v2 connected through its CMSIS-DAP debugger
- Bluetooth support on the host for the integration clients

## Setup

```sh
mise trust
mise install
mise run setup
mise run update
```

`mise run setup` creates `.venv` and installs Zephyr and Python dependencies. `mise run update` fetches West projects, including actor_framework_cxx.

## Build and flash

```sh
mise run build
mise run flash
```

The board is reset after flashing and advertises as `klab BLE` for 30 seconds.

## BLE integration clients

Activate the virtual environment, reset the board, then run a client before advertising expires:

```sh
source .venv/bin/activate
probe-rs reset --chip nRF52833_xxAA
python tests/integration/echo_client.py --name='klab BLE'
```

Other clients:

```sh
python tests/integration/ping_client.py --name='klab BLE'
python tests/integration/symbol_client.py --name='klab BLE'
```

The clients share BLE transport and protobuf-generation code in [`tests/integration/ble_client.py`](tests/integration/ble_client.py). See [`tests/integration/README.md`](tests/integration/README.md) for details.

## Useful tasks

```sh
mise run menuconfig
mise run clean
```

## BLE service

| Item | UUID |
| --- | --- |
| Service | `7b5a0001-4f1d-4c8b-8d4a-5f4d7a123000` |
| RX characteristic (write) | `7b5a0002-4f1d-4c8b-8d4a-5f4d7a123000` |
| TX characteristic (notify) | `7b5a0003-4f1d-4c8b-8d4a-5f4d7a123000` |

Each characteristic payload is a serialized `BleChunk`. The chunk data contains a serialized protobuf request or response envelope.
