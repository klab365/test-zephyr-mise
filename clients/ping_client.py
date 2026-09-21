#!/usr/bin/env python3
import argparse
import asyncio

from data import PingRequest, PingResponse
from smp_client import connected_client


async def run(args: argparse.Namespace) -> None:
    client = await connected_client(args.name)
    try:
        request = PingRequest(args.sequence)
        response = PingResponse.from_cbor(
            await client.request(request.command, request.to_cbor())
        )
        print(f"pong sequence={response.sequence}")
    finally:
        await client.close()


parser = argparse.ArgumentParser(description="Send a Ping request via SMP-over-BLE.")
parser.add_argument("--name", required=True, help="BLE local name")
parser.add_argument("--sequence", type=int, default=1, help="unsigned ping sequence")

if __name__ == "__main__":
    asyncio.run(run(parser.parse_args()))
