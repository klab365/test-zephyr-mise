"""Minimal SMP-over-BLE client for the application's custom mcumgr group."""

import asyncio
import struct
from typing import Any

import cbor2
from bleak import BleakClient, BleakScanner

from data import (
    APP_GROUP_ID,
    SMP_CHARACTERISTIC_UUID,
    SMP_HEADER_SIZE,
    SMP_OP_WRITE,
    SMP_OP_WRITE_RESPONSE,
    SMP_SERVICE_UUID,
    SMP_VERSION_2,
)


class SmpError(RuntimeError):
    """An SMP response was malformed or contained an mcumgr error."""


class SmpBleClient:
    def __init__(self, client: BleakClient):
        self._client = client
        self._sequence = 0
        self._response = bytearray()
        self._response_ready = asyncio.Event()
        self._request_lock = asyncio.Lock()

    @classmethod
    async def connect_by_name(cls, name: str, timeout: float = 10.0) -> "SmpBleClient":
        device = await BleakScanner.find_device_by_filter(
            lambda device, advertisement: device.name == name
            or advertisement.local_name == name,
            timeout=timeout,
        )
        if device is None:
            raise RuntimeError(f"SMP device not found: {name}")

        client = BleakClient(device)
        await client.connect()
        endpoint = cls(client)
        await client.start_notify(SMP_CHARACTERISTIC_UUID, endpoint._notification)
        return endpoint

    async def close(self) -> None:
        if self._client.is_connected:
            await self._client.stop_notify(SMP_CHARACTERISTIC_UUID)
            await self._client.disconnect()

    def _notification(self, _: int, data: bytearray) -> None:
        self._response.extend(data)
        if len(self._response) < SMP_HEADER_SIZE:
            return

        _, _, payload_length, _, _, _ = struct.unpack(
            ">BBHHBB", self._response[:SMP_HEADER_SIZE]
        )
        if len(self._response) >= SMP_HEADER_SIZE + payload_length:
            self._response_ready.set()

    def _write_chunk_size(self) -> int:
        characteristic = self._client.services.get_characteristic(SMP_CHARACTERISTIC_UUID)
        if characteristic is None:
            raise SmpError("SMP characteristic was not discovered")
        # Bleak reports 20 until MTU negotiation has completed on some backends.
        return max(20, characteristic.max_write_without_response_size)

    async def request(self, command: int, payload: dict[int, Any], timeout: float = 10.0) -> dict[int, Any]:
        """Send one SMP write request and return its decoded CBOR response map."""
        async with self._request_lock:
            encoded_payload = cbor2.dumps(payload)
            sequence = self._sequence
            self._sequence = (self._sequence + 1) & 0xFF
            header = struct.pack(
                ">BBHHBB",
                SMP_OP_WRITE | (SMP_VERSION_2 << 3),
                0,
                len(encoded_payload),
                APP_GROUP_ID,
                sequence,
                command,
            )
            frame = header + encoded_payload
            self._response.clear()
            self._response_ready.clear()

            for offset in range(0, len(frame), self._write_chunk_size()):
                await self._client.write_gatt_char(
                    SMP_CHARACTERISTIC_UUID,
                    frame[offset : offset + self._write_chunk_size()],
                    response=False,
                )

            await asyncio.wait_for(self._response_ready.wait(), timeout)
            response = bytes(self._response)
            op, _, payload_length, group, response_sequence, response_command = struct.unpack(
                ">BBHHBB", response[:SMP_HEADER_SIZE]
            )
            if (op & 0x07) != SMP_OP_WRITE_RESPONSE or group != APP_GROUP_ID:
                raise SmpError("unexpected SMP response")
            if response_sequence != sequence or response_command != command:
                raise SmpError("SMP response does not match the request")

            decoded = cbor2.loads(response[SMP_HEADER_SIZE : SMP_HEADER_SIZE + payload_length])
            if "err" in decoded or "rc" in decoded:
                raise SmpError(f"mcumgr command failed: {decoded}")
            return decoded


async def connected_client(name: str) -> SmpBleClient:
    return await SmpBleClient.connect_by_name(name)
