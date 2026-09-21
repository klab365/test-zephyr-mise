#!/usr/bin/env python3
import argparse
import asyncio

from data import EchoRequest, EchoResponse
from smp_client import connected_client


async def run(args: argparse.Namespace) -> None:
    client = await connected_client(args.name)
    try:
        request = EchoRequest(args.payload.encode())
        response = EchoResponse.from_cbor(
            await client.request(request.command, request.to_cbor())
        )
        print(response.payload.decode(errors="replace"))
    finally:
        await client.close()


parser = argparse.ArgumentParser(description="Send an Echo request via SMP-over-BLE.")
parser.add_argument("--name", required=True, help="BLE local name")
parser.add_argument("--payload", required=True, help="UTF-8 payload to echo")

if __name__ == "__main__":
    asyncio.run(run(parser.parse_args()))
