#!/usr/bin/env python3
"""Extract the Pico CYW43439 firmware and CLM from Pico SDK's combined header."""

from __future__ import annotations

import argparse
import pathlib
import re


def parse_define(source: str, name: str) -> int:
    match = re.search(rf"#define\s+{name}\s+\((\d+)\)", source)
    if match is None:
        raise ValueError(f"{name} was not found")
    return int(match.group(1))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("header", type=pathlib.Path)
    parser.add_argument("output_directory", type=pathlib.Path)
    args = parser.parse_args()

    source = args.header.read_text(encoding="utf-8")
    array_match = re.search(r"\{(.*)\};", source, flags=re.DOTALL)
    if array_match is None:
        raise ValueError("combined firmware byte array was not found")


    array_source = re.sub(r"/\*.*?\*/", "", array_match.group(1), flags=re.DOTALL)
    array_source = re.sub(r"//.*?$", "", array_source, flags=re.MULTILINE)
    image = bytes(
        int(value, 16)
        for value in re.findall(r"0x([0-9a-fA-F]{2})", array_source)
    )
    firmware_length = parse_define(source, "CYW43_WIFI_FW_LEN")
    clm_length = parse_define(source, "CYW43_CLM_LEN")
    clm_offset = (firmware_length + 511) & ~511
    expected_length = clm_offset + clm_length
    if len(image) != expected_length:
        raise ValueError(
            f"combined image is {len(image)} bytes; expected {expected_length}"
        )

    args.output_directory.mkdir(parents=True, exist_ok=True)
    (args.output_directory / "pico2w-cyw43439-firmware.bin").write_bytes(
        image[:firmware_length]
    )
    (args.output_directory / "pico2w-cyw43439.clm_blob").write_bytes(
        image[clm_offset:]
    )


if __name__ == "__main__":
    main()
