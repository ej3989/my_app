#!/usr/bin/env python3
"""LAN-only MCUboot OTA helper for the Pico 2 W aging controller."""

import argparse
import asyncio
import logging
from pathlib import Path
import sys

from smpclient import SMPClient
from smpclient.generics import error
from smpclient.requests.image_management import ImageStatesRead, ImageStatesWrite
from smpclient.requests.os_management import EchoWrite, ResetWrite
from smpclient.transport.udp import SMPUDPTransport

# The library logs the full binary request on a timeout, which can be hundreds
# of terminal lines. This tool reports concise retry/final errors itself.
logging.getLogger("smpclient").setLevel(logging.CRITICAL)


def print_response_error(response: object) -> None:
    print(f"SMP request failed: {response}", file=sys.stderr)


def print_images(response: object) -> None:
    images = getattr(response, "images", [])
    if not images:
        print("No MCUboot image slots were returned")
        return

    print("slot  version        active  pending  confirmed  bootable  hash")
    for image in images:
        digest = bytes(image.hash).hex() if image.hash is not None else "-"
        print(
            f"{image.slot:<5} {image.version:<14} "
            f"{str(bool(image.active)):<7} {str(bool(image.pending)):<8} "
            f"{str(bool(image.confirmed)):<10} {str(bool(image.bootable)):<8} "
            f"{digest}"
        )


async def read_images(client: SMPClient):
    response = await client.request(ImageStatesRead())
    if error(response):
        print_response_error(response)
        raise RuntimeError("failed to read image state")
    return response


async def upload_with_retries(args: argparse.Namespace) -> None:
    image_path = Path(args.image)
    image = image_path.read_bytes()
    print(f"Uploading {image_path} ({len(image)} bytes) to the secondary slot")

    last_percent = -1
    for attempt in range(1, args.retries + 2):
        try:
            # Reconnect for every retry so a delayed UDP response from the
            # previous attempt cannot be mistaken for the new SMP sequence.
            async with SMPClient(
                SMPUDPTransport(mtu=args.mtu), args.host, timeout_s=args.timeout
            ) as client:
                async for offset in client.upload(
                    image,
                    slot=0,
                    upgrade=False,
                    subsequent_timeout_s=args.timeout,
                ):
                    percent = min(100, offset * 100 // len(image))
                    if percent != last_percent and (percent % 5 == 0 or percent == 100):
                        print(f"{percent}% ({offset}/{len(image)} bytes)")
                        last_percent = percent

                print("Upload complete; the image is not active yet")
                print_images(await read_images(client))
                return
        except TimeoutError:
            if attempt > args.retries:
                raise

            print(
                f"UDP response timed out; reconnecting to resume upload "
                f"({attempt}/{args.retries})",
                file=sys.stderr,
            )
            await asyncio.sleep(1)


async def run(args: argparse.Namespace) -> None:
    if args.command == "upload":
        await upload_with_retries(args)
        return

    async with SMPClient(
        SMPUDPTransport(mtu=args.mtu), args.host, timeout_s=args.timeout
    ) as client:
        if args.command == "probe":
            response = await client.request(EchoWrite(d="wifi_fan_ota_probe"))
            if error(response):
                print_response_error(response)
                raise RuntimeError("OTA probe failed")
            print("SMP/UDP connection OK")
            print_images(await read_images(client))
            return

        if args.command == "list":
            print_images(await read_images(client))
            return

        states = await read_images(client)
        images = list(states.images)

        if args.command == "test":
            candidates = [image for image in images if not image.active and image.hash is not None]
            if len(candidates) != 1:
                raise RuntimeError(
                    f"expected exactly one inactive update image, found {len(candidates)}"
                )
            candidate = candidates[0]
            response = await client.request(
                ImageStatesWrite(hash=bytes(candidate.hash), confirm=False)
            )
            if error(response):
                print_response_error(response)
                raise RuntimeError("failed to mark image for test boot")
            print(f"Slot {candidate.slot} marked for one test boot")
            print_images(response)
            return

        if args.command == "confirm":
            active = [image for image in images if image.active and image.hash is not None]
            if len(active) != 1:
                raise RuntimeError(f"expected one active image, found {len(active)}")
            response = await client.request(
                ImageStatesWrite(hash=bytes(active[0].hash), confirm=True)
            )
            if error(response):
                print_response_error(response)
                raise RuntimeError("failed to confirm active image")
            print("Active image confirmed")
            print_images(response)
            return

        if args.command == "reset":
            response = await client.request(ResetWrite())
            if error(response):
                print_response_error(response)
                raise RuntimeError("remote reset failed")
            print("Reset accepted; the device is rebooting")
            return

        raise RuntimeError(f"unsupported command: {args.command}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Pico 2 W MCUboot OTA over unauthenticated LAN-only SMP/UDP port 1337"
    )
    parser.add_argument("host", help="Pico 2 W IPv4 address on the same LAN")
    parser.add_argument("command", choices=("probe", "list", "upload", "test", "reset", "confirm"))
    parser.add_argument("image", nargs="?", help="zephyr.signed.bin (required for upload)")
    parser.add_argument("--timeout", type=float, default=10.0, help="SMP request timeout in seconds")
    parser.add_argument("--retries", type=int, default=5, help="upload timeout retries")
    parser.add_argument("--mtu", type=int, default=1500, help="SMP/UDP transport MTU")
    args = parser.parse_args()

    if args.retries < 0:
        parser.error("--retries must be zero or greater")
    if args.mtu < 128 or args.mtu > 1500:
        parser.error("--mtu must be between 128 and 1500")

    if args.command == "upload" and args.image is None:
        parser.error("upload requires a zephyr.signed.bin path")
    if args.command != "upload" and args.image is not None:
        parser.error("an image path is only valid with the upload command")

    try:
        asyncio.run(run(args))
    except Exception as exc:
        print(f"OTA error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
