#!/usr/bin/env python3

import argparse
import asyncio

from ble_client import app_protocol_pb2, connected_endpoint


async def run(args):
    async with connected_endpoint(args.name) as endpoint:
        endpoint.print_connection_info()

        for offset in range(args.count):
            request_id = args.request_id + offset
            payload = f"{args.payload}-{offset + 1}" if args.count > 1 else args.payload
            request = app_protocol_pb2.RequestEnvelope(request_id=request_id)
            request.echo.payload = payload.encode("utf-8")

            print(f"req:  id={request_id} echo={payload}")
            response = await endpoint.request(
                request_id, request.SerializeToString(), args.chunk_size, args.timeout
            )

            if response.WhichOneof("payload") != "echo":
                raise RuntimeError("response was not echo payload")

            response_payload = response.echo.payload.decode("utf-8")
            if response_payload != payload:
                raise RuntimeError(f"unexpected echo payload: {response_payload}")

            print(f"resp: id={response.request_id} echo={response_payload}")


def parse_args():
    parser = argparse.ArgumentParser(description="Send an EchoRequest over BLE.")
    parser.add_argument("--name", required=True, help="BLE device local name")
    parser.add_argument("--payload", default="hello", help="UTF-8 payload to echo")
    parser.add_argument("--count", type=int, default=5, help="Number of echo requests to send")
    parser.add_argument("--chunk-size", type=int, default=128, help="Max protobuf bytes per BLE chunk")
    parser.add_argument("--request-id", type=int, default=1, help="Request correlation ID")
    parser.add_argument("--timeout", type=float, default=5.0, help="Response timeout in seconds")
    return parser.parse_args()


if __name__ == "__main__":
    asyncio.run(run(parse_args()))
