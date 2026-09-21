"""Wire definitions for the application's custom mcumgr group."""

from dataclasses import dataclass
from enum import IntEnum
from typing import Any, ClassVar

APP_GROUP_ID = 64
# SMP v2 is encoded as enum value 1 (v1/original is 0) in the header.
SMP_VERSION_2 = 1
SMP_HEADER_SIZE = 8
SMP_OP_WRITE = 2
SMP_OP_WRITE_RESPONSE = 3

SMP_SERVICE_UUID = "8d53dc1d-1db7-4cd3-868b-8a527460aa84"
SMP_CHARACTERISTIC_UUID = "da2e7828-fbce-4e01-ae9e-261174997c48"


class AppCommand(IntEnum):
    ECHO = 0
    PING = 1
    SET_MATRIX_SYMBOL = 2


class MatrixSymbol(IntEnum):
    OFF = 1
    SMILE = 2
    HEART = 3
    CHECK = 4
    CROSS = 5


@dataclass(frozen=True)
class EchoRequest:
    payload: bytes
    command: ClassVar[AppCommand] = AppCommand.ECHO

    def to_cbor(self) -> dict[int, Any]:
        return {1: self.payload}

    @classmethod
    def from_cbor(cls, value: dict[int, Any]) -> "EchoRequest":
        return cls(payload=bytes(value[1]))


@dataclass(frozen=True)
class EchoResponse:
    payload: bytes

    @classmethod
    def from_cbor(cls, value: dict[int, Any]) -> "EchoResponse":
        return cls(payload=bytes(value[1]))


@dataclass(frozen=True)
class PingRequest:
    sequence: int
    command: ClassVar[AppCommand] = AppCommand.PING

    def to_cbor(self) -> dict[int, Any]:
        return {1: self.sequence}


@dataclass(frozen=True)
class PingResponse:
    sequence: int

    @classmethod
    def from_cbor(cls, value: dict[int, Any]) -> "PingResponse":
        return cls(sequence=int(value[1]))


@dataclass(frozen=True)
class SetMatrixSymbolRequest:
    symbol: MatrixSymbol
    command: ClassVar[AppCommand] = AppCommand.SET_MATRIX_SYMBOL

    def to_cbor(self) -> dict[int, Any]:
        return {1: int(self.symbol)}


@dataclass(frozen=True)
class SetMatrixSymbolResponse:
    symbol: MatrixSymbol

    @classmethod
    def from_cbor(cls, value: dict[int, Any]) -> "SetMatrixSymbolResponse":
        return cls(symbol=MatrixSymbol(int(value[1])))
