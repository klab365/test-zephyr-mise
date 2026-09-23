#!/usr/bin/env python3
"""Download the file_actor's dummy file over the BLE protobuf transport."""

import argparse
import asyncio
import pathlib

from ble_client import app_protocol_pb2, connected_endpoint


async def run(args):
    output = pathlib.Path(args.output)
    offset = 0
    request_id = args.request_id

    async with connected_endpoint(args.name) as endpoint:
        endpoint.print_connection_info()

        with output.open("wb") as file:
            while True:
                request = app_protocol_pb2.RequestEnvelope(request_id=request_id)
                request.file_read.transfer_id = args.transfer_id
                request.file_read.offset = offset
                request.file_read.max_bytes = args.block_size

                response = await endpoint.request(
                    request_id, request.SerializeToString(), args.chunk_size, args.timeout
                )
                if response.WhichOneof("payload") != "file_data":
                    raise RuntimeError("expected FileDataResponse")

                file_data = response.file_data
                if file_data.transfer_id != args.transfer_id or file_data.offset != offset:
                    raise RuntimeError("unexpected file transfer ID or offset")
                if not file_data.data and not file_data.final:
                    raise RuntimeError("received an empty non-final file block")

                expected = bytes((offset + index) & 0xFF for index in range(len(file_data.data)))
                if file_data.data != expected:
                    raise RuntimeError("dummy file data verification failed")

                file.write(file_data.data)
                offset += len(file_data.data)
                print(f"received {offset} bytes")

                if file_data.final:
                    break
                request_id += 1

    print(f"saved {offset} bytes to {output}")


def parse_args():
    parser = argparse.ArgumentParser(description="Download the BLE dummy file.")
    parser.add_argument("--name", required=True, help="BLE device local name")
    parser.add_argument("--output", default="dummy.bin", help="Destination file path")
    parser.add_argument("--transfer-id", type=int, default=1, help="File transfer ID")
    parser.add_argument("--request-id", type=int, default=1, help="Initial request correlation ID")
    parser.add_argument("--block-size", type=int, default=256, help="Requested bytes per file block")
    parser.add_argument("--chunk-size", type=int, default=227, help="Max protobuf bytes per BLE chunk")
    parser.add_argument("--timeout", type=float, default=5.0, help="Request timeout in seconds")
    return parser.parse_args()


if __name__ == "__main__":
    asyncio.run(run(parse_args()))
