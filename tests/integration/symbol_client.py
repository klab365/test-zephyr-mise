#!/usr/bin/env python3

import argparse
import asyncio

from ble_client import app_protocol_pb2, connected_endpoint


SYMBOLS = (
    ("smile", app_protocol_pb2.MATRIX_SYMBOL_SMILE),
    ("heart", app_protocol_pb2.MATRIX_SYMBOL_HEART),
    ("check", app_protocol_pb2.MATRIX_SYMBOL_CHECK),
    ("cross", app_protocol_pb2.MATRIX_SYMBOL_CROSS),
    ("off", app_protocol_pb2.MATRIX_SYMBOL_OFF),
)


async def run(args):
    async with connected_endpoint(args.name) as endpoint:
        endpoint.print_connection_info()

        for offset, (name, symbol) in enumerate(SYMBOLS):
            request_id = args.request_id + offset
            request = app_protocol_pb2.RequestEnvelope(request_id=request_id)
            request.set_matrix_symbol.symbol = symbol

            print(f"req:  id={request_id} symbol={name}")
            response = await endpoint.request(
                request_id, request.SerializeToString(), args.chunk_size, args.timeout
            )

            if response.WhichOneof("payload") != "set_matrix_symbol":
                raise RuntimeError("response was not set_matrix_symbol payload")
            if response.set_matrix_symbol.symbol != symbol:
                raise RuntimeError(
                    f"unexpected symbol response: {response.set_matrix_symbol.symbol}"
                )

            print(f"resp: id={response.request_id} symbol={name}")
            await asyncio.sleep(args.hold_seconds)


def parse_args():
    parser = argparse.ArgumentParser(description="Display each matrix symbol over BLE.")
    parser.add_argument("--name", required=True, help="BLE device local name")
    parser.add_argument("--request-id", type=int, default=1, help="Initial request correlation ID")
    parser.add_argument("--chunk-size", type=int, default=128, help="Max protobuf bytes per BLE chunk")
    parser.add_argument("--timeout", type=float, default=5.0, help="Response timeout in seconds")
    parser.add_argument(
        "--hold-seconds", type=float, default=2.0, help="Seconds each symbol remains active"
    )
    return parser.parse_args()


if __name__ == "__main__":
    asyncio.run(run(parse_args()))
