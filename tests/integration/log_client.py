#!/usr/bin/env python3
"""Download offset-paginated demo logs and save them as JSON Lines."""

import argparse
import asyncio
import json
import pathlib

from ble_client import app_protocol_pb2, connected_endpoint


async def run(args):
    output = pathlib.Path(args.output)
    offset = 0
    request_id = args.request_id

    async with connected_endpoint(args.name) as endpoint:
        endpoint.print_connection_info()

        with output.open("w", encoding="utf-8") as file:
            while True:
                request = app_protocol_pb2.RequestEnvelope(request_id=request_id)
                request.get_logs.offset = offset
                request.get_logs.max_entries = args.max_entries

                response = await endpoint.request(
                    request_id, request.SerializeToString(), args.chunk_size, args.timeout
                )
                if response.WhichOneof("payload") != "get_logs":
                    raise RuntimeError("expected GetLogsResponse")

                logs = response.get_logs
                if logs.next_offset < offset:
                    raise RuntimeError("response next_offset moved backwards")

                for entry in logs.entries:
                    json.dump(
                        {
                            "sequence": entry.sequence,
                            "timestamp_ms": entry.timestamp_ms,
                            "level": entry.level,
                            "message": entry.message,
                        },
                        file,
                    )
                    file.write("\n")

                print(f"received {len(logs.entries)} entries; next offset={logs.next_offset}")
                if logs.final:
                    break
                if logs.next_offset == offset:
                    raise RuntimeError("response made no progress")

                offset = logs.next_offset
                request_id += 1

    print(f"saved logs to {output}")


def parse_args():
    parser = argparse.ArgumentParser(description="Download paginated BLE demo logs.")
    parser.add_argument("--name", required=True, help="BLE device local name")
    parser.add_argument("--output", default="logs.jsonl", help="Destination JSON Lines file")
    parser.add_argument("--max-entries", type=int, default=4, help="Entries requested per page")
    parser.add_argument("--request-id", type=int, default=1, help="Initial request correlation ID")
    parser.add_argument("--chunk-size", type=int, default=227, help="Max protobuf bytes per BLE chunk")
    parser.add_argument("--timeout", type=float, default=5.0, help="Request timeout in seconds")
    return parser.parse_args()


if __name__ == "__main__":
    asyncio.run(run(parse_args()))
