#!/usr/bin/env python3
import argparse
import asyncio

from data import MatrixSymbol, SetMatrixSymbolRequest, SetMatrixSymbolResponse
from smp_client import connected_client


async def run(args: argparse.Namespace) -> None:
    symbol = MatrixSymbol[args.symbol.upper()]
    client = await connected_client(args.name)
    try:
        request = SetMatrixSymbolRequest(symbol)
        response = SetMatrixSymbolResponse.from_cbor(
            await client.request(request.command, request.to_cbor())
        )
        print(f"matrix symbol={response.symbol.name.lower()}")
    finally:
        await client.close()


parser = argparse.ArgumentParser(description="Set the LED matrix symbol via SMP-over-BLE.")
parser.add_argument("--name", required=True, help="BLE local name")
parser.add_argument("--symbol", choices=[symbol.name.lower() for symbol in MatrixSymbol], required=True)

if __name__ == "__main__":
    asyncio.run(run(parser.parse_args()))
