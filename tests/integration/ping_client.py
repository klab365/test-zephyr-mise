#!/usr/bin/env python3

import argparse
import asyncio

from ble_client import app_protocol_pb2, connected_endpoint


async def run(args):
    async with connected_endpoint(args.name) as endpoint:
        endpoint.print_connection_info()

        for offset in range(args.count):
            request_id = args.request_id + offset
            sequence = args.sequence + offset
            request = app_protocol_pb2.RequestEnvelope(request_id=request_id)
            request.ping.sequence = sequence

            print(f"req:  id={request_id} ping={sequence}")
            response = await endpoint.request(
                request_id, request.SerializeToString(), args.chunk_size, args.timeout
            )

            if response.WhichOneof("payload") != "ping":
                raise RuntimeError("response was not ping payload")
            if response.ping.sequence != sequence:
                raise RuntimeError(
                    f"unexpected ping sequence: {response.ping.sequence}, expected {sequence}"
                )

            print(f"resp: id={response.request_id} ping={response.ping.sequence}")


def parse_args():
    parser = argparse.ArgumentParser(description="Send a PingRequest over BLE.")
    parser.add_argument("--name", required=True, help="BLE device local name")
    parser.add_argument("--count", type=int, default=5, help="Number of ping requests to send")
    parser.add_argument("--chunk-size", type=int, default=128, help="Max protobuf bytes per BLE chunk")
    parser.add_argument("--request-id", type=int, default=1, help="Initial request correlation ID")
    parser.add_argument("--sequence", type=int, default=1, help="Initial ping sequence")
    parser.add_argument("--timeout", type=float, default=5.0, help="Response timeout in seconds")
    return parser.parse_args()


if __name__ == "__main__":
    asyncio.run(run(parse_args()))
