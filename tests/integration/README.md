# BLE Integration Tests

This folder contains host-side BLE integration tools that use the same protobuf
schema as the firmware.

Install dependencies:

```sh
python -m pip install -r requirements.txt
```

`ble_client.py` provides shared BLE framing, notification reassembly, and
protobuf generation for the clients. It regenerates Python protobuf modules
when the schemas change.

Run the echo client:

```sh
python echo_client.py --name "klab BLE" --payload hello
```

Run the ping client:

```sh
python ping_client.py --name "klab BLE"
```

Run the symbol client. It displays smile, heart, check, cross, and off in
sequence, holding each symbol for one second:

```sh
python symbol_client.py --name "klab BLE"
```

The GATT frame format is:

```text
protobuf BleChunk bytes
```

Each `BleChunk.data` contains part of a serialized `app_protocol.proto`
`RequestEnvelope` or `ResponseEnvelope`. Request dispatch happens from the
reassembled envelope `oneof` payload.
