#!/usr/bin/env python3
"""Acquire a five-second measurement curve, download it, validate it, and save JSON."""

import argparse
import asyncio
import json
import pathlib
import zlib

from ble_client import app_protocol_pb2, connected_endpoint


async def request(endpoint, request_id, envelope, args):
    return await endpoint.request(
        request_id, envelope.SerializeToString(), args.chunk_size, args.timeout
    )


async def run(args):
    request_id = args.request_id
    async with connected_endpoint(args.name) as endpoint:
        endpoint.print_connection_info()

        start = app_protocol_pb2.RequestEnvelope(request_id=request_id)
        start.start_measurement.CopyFrom(app_protocol_pb2.StartMeasurementRequest())
        response = await request(endpoint, request_id, start, args)
        if response.WhichOneof("payload") != "start_measurement":
            raise RuntimeError("expected StartMeasurementResponse")
        print(f"started {response.start_measurement.file_id}")

        await asyncio.sleep(args.duration)
        request_id += 1
        stop = app_protocol_pb2.RequestEnvelope(request_id=request_id)
        stop.stop_measurement.CopyFrom(app_protocol_pb2.StopMeasurementRequest())
        response = await request(endpoint, request_id, stop, args)
        if response.WhichOneof("payload") != "stop_measurement":
            raise RuntimeError("expected StopMeasurementResponse")
        print(f"stopped {response.stop_measurement.file_id}")

        encoded_curve = bytearray()
        offset = 0
        request_id += 1
        while True:
            download = app_protocol_pb2.RequestEnvelope(request_id=request_id)
            download.get_measurement_curve.transfer_id = args.transfer_id
            download.get_measurement_curve.offset = offset
            response = await request(endpoint, request_id, download, args)
            if response.WhichOneof("payload") != "get_measurement_curve":
                raise RuntimeError("expected GetMeasurementCurveResponse")

            block = response.get_measurement_curve
            if block.transfer_id != args.transfer_id or block.offset != offset:
                raise RuntimeError("unexpected measurement-curve block")
            encoded_curve.extend(block.data)
            offset += len(block.data)
            print(f"downloaded {offset} bytes")
            if block.final:
                break
            request_id += 1

    print(f"download complete: {len(encoded_curve)} bytes")
    if len(encoded_curve) < 4:
        raise RuntimeError("downloaded curve has no CRC trailer")
    stored_crc = int.from_bytes(encoded_curve[-4:], byteorder="little")
    curve_bytes = bytes(encoded_curve[:-4])
    curve = app_protocol_pb2.MeasurementCurve()
    curve.ParseFromString(curve_bytes)
    calculated_crc = zlib.crc32(curve_bytes) & 0xFFFFFFFF
    if calculated_crc != stored_crc:
        raise RuntimeError(
            f"CRC mismatch: stored=0x{stored_crc:08x}, calculated=0x{calculated_crc:08x}"
        )
    records = memoryview(curve.measurement_records)
    measurements = []
    position = 0
    while position < len(records):
        length = 0
        shift = 0
        while True:
            if position >= len(records) or shift >= 35:
                raise RuntimeError("invalid measurement record length")
            byte = records[position]
            position += 1
            length |= (byte & 0x7F) << shift
            if not byte & 0x80:
                break
            shift += 7
        if position + length > len(records):
            raise RuntimeError("truncated measurement record")
        measurement = app_protocol_pb2.Measurement()
        measurement.ParseFromString(records[position : position + length])
        measurements.append({"temperature": measurement.temperature, "humidity": measurement.humidity})
        position += length
    if curve.count != len(measurements):
        raise RuntimeError("curve count does not match measurement count")

    output = {
        "started_timestamp_ms": curve.started_timestamp_ms,
        "count": curve.count,
        "crc32": f"0x{stored_crc:08x}",
        "measurements": measurements,
    }
    pathlib.Path(args.output).write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print(f"saved {curve.count} measurements to {args.output}")


def parse_args():
    parser = argparse.ArgumentParser(description="Acquire and download a measurement curve.")
    parser.add_argument("--name", required=True, help="BLE device local name")
    parser.add_argument("--output", default="measurement_curve.json", help="JSON destination")
    parser.add_argument("--duration", type=float, default=10.0, help="Acquisition duration in seconds")
    parser.add_argument("--transfer-id", type=int, default=1, help="Download transfer ID")
    parser.add_argument("--request-id", type=int, default=1, help="Initial request correlation ID")
    parser.add_argument("--chunk-size", type=int, default=227, help="Max protobuf bytes per BLE chunk")
    parser.add_argument("--timeout", type=float, default=5.0, help="Request timeout in seconds")
    return parser.parse_args()


if __name__ == "__main__":
    asyncio.run(run(parse_args()))
