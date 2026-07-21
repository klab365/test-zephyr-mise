"""Shared BLE transport support for endpoint integration clients."""

import asyncio
import importlib
import pathlib
import subprocess
import sys
from contextlib import asynccontextmanager

from bleak import BleakClient, BleakScanner


SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
PROTO_DIR = REPO_ROOT / "app" / "assets" / "proto"
PROTO_FILES = [PROTO_DIR / "ble_transport.proto", PROTO_DIR / "app_protocol.proto"]
GENERATED_PROTOS = [SCRIPT_DIR / "ble_transport_pb2.py", SCRIPT_DIR / "app_protocol_pb2.py"]

SERVICE_UUID = "7b5a0001-4f1d-4c8b-8d4a-5f4d7a123000"
RX_UUID = "7b5a0002-4f1d-4c8b-8d4a-5f4d7a123000"
TX_UUID = "7b5a0003-4f1d-4c8b-8d4a-5f4d7a123000"


def load_proto_modules():
    """Generate Python protobuf bindings when they are absent or stale."""
    generated_stale = not all(path.exists() for path in GENERATED_PROTOS)
    if not generated_stale:
        newest_proto = max(path.stat().st_mtime for path in PROTO_FILES)
        oldest_generated = min(path.stat().st_mtime for path in GENERATED_PROTOS)
        generated_stale = oldest_generated < newest_proto

    if generated_stale:
        subprocess.run(
            [
                sys.executable,
                "-m",
                "grpc_tools.protoc",
                "-I",
                str(PROTO_DIR),
                "--python_out",
                str(SCRIPT_DIR),
                *[str(path) for path in PROTO_FILES],
            ],
            check=True,
        )

    sys.path.insert(0, str(SCRIPT_DIR))
    return importlib.import_module("ble_transport_pb2"), importlib.import_module("app_protocol_pb2")


ble_transport_pb2, app_protocol_pb2 = load_proto_modules()


async def find_device(name: str):
    device = await BleakScanner.find_device_by_filter(
        lambda dev, adv: dev.name == name or adv.local_name == name,
        timeout=10.0,
    )
    if device is None:
        raise RuntimeError(f"BLE device not found: {name}")
    return device


class BleEndpointClient:
    def __init__(self, client: BleakClient):
        self.client = client
        self.loop = asyncio.get_running_loop()
        self.response_futures = {}
        self.response_chunks = {}
        self.mtu_size = getattr(client, "mtu_size", None)
        self.max_write_without_response_size = None

    def on_notify(self, _sender, data: bytearray):
        chunk = ble_transport_pb2.BleChunk()
        chunk.ParseFromString(bytes(data))

        chunks = self.response_chunks.setdefault(chunk.message_id, bytearray(chunk.total_size))
        chunks[chunk.offset : chunk.offset + len(chunk.data)] = chunk.data

        if not chunk.final:
            return

        envelope = app_protocol_pb2.ResponseEnvelope()
        envelope.ParseFromString(bytes(chunks))
        self.response_chunks.pop(chunk.message_id, None)

        response_future = self.response_futures.get(envelope.request_id)
        if response_future is not None and not response_future.done():
            response_future.set_result(envelope)

    async def write_chunked(self, message_id: int, payload: bytes, max_payload_size: int):
        offset = 0
        while offset < len(payload):
            chunk = ble_transport_pb2.BleChunk(
                message_id=message_id,
                offset=offset,
                total_size=len(payload),
                data=payload[offset : offset + max_payload_size],
                final=(offset + max_payload_size) >= len(payload),
            )
            chunk_bytes = chunk.SerializeToString()
            if self.mtu_size is not None and len(chunk_bytes) > (self.mtu_size - 3):
                raise RuntimeError(
                    f"encoded chunk is {len(chunk_bytes)} bytes, larger than ATT payload "
                    f"{self.mtu_size - 3}"
                )

            await self.client.write_gatt_char(RX_UUID, chunk_bytes, response=True)
            offset += len(chunk.data)

    async def request(self, request_id: int, payload: bytes, chunk_size: int, timeout: float):
        if request_id in self.response_futures:
            raise RuntimeError(f"request ID is already pending: {request_id}")

        response_future = self.loop.create_future()
        self.response_futures[request_id] = response_future
        try:
            await self.write_chunked(request_id, payload, chunk_size)
            return await asyncio.wait_for(response_future, timeout=timeout)
        finally:
            self.response_futures.pop(request_id, None)

    def print_connection_info(self):
        if self.mtu_size is not None:
            print(f"mtu:  {self.mtu_size} bytes, notification payload <= {self.mtu_size - 3} bytes")
        else:
            print("mtu:  unavailable from this Bleak backend")

        if self.max_write_without_response_size is not None:
            print(f"write-without-response payload <= {self.max_write_without_response_size}")


@asynccontextmanager
async def connected_endpoint(name: str):
    device = await find_device(name)
    async with BleakClient(device) as client:
        endpoint = BleEndpointClient(client)
        get_services = getattr(client, "get_services", None)
        services = await get_services() if get_services is not None else client.services
        rx_char = services.get_characteristic(RX_UUID)
        if rx_char is None:
            raise RuntimeError(f"RX characteristic not found: {RX_UUID}")

        endpoint.max_write_without_response_size = getattr(
            rx_char, "max_write_without_response_size", None
        )
        await client.start_notify(TX_UUID, endpoint.on_notify)
        try:
            yield endpoint
        finally:
            await client.stop_notify(TX_UUID)
