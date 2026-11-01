#!/usr/bin/env python3
"""Create the exact JSON bytes signed for an EchoDesk OS GitHub release."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import pathlib
import re

REPOSITORY = "andomatthew1234/EchoDesk"
VERSION_RE = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-[0-9A-Za-z.-]+)?$")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", required=True, type=pathlib.Path)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--minimum-desktop-version", default="0.2.8")
    parser.add_argument("--release-notes")
    args = parser.parse_args()

    prefix = "fw-beta-v" if args.tag.startswith("fw-beta-v") else "fw-v"
    if not args.tag.startswith(prefix):
        raise SystemExit("firmware tag must use fw-vVERSION or fw-beta-vVERSION")
    version = args.tag.removeprefix(prefix)
    if not VERSION_RE.fullmatch(version):
        raise SystemExit(f"invalid firmware tag version: {version}")
    if not VERSION_RE.fullmatch(args.minimum_desktop_version):
        raise SystemExit("invalid minimum Desktop version")

    expected_name = f"echodesk-os-freenove-fnk0114a-2p4-v{version}.bin"
    if args.image.name != expected_name:
        raise SystemExit(f"firmware image must be named {expected_name}")
    image = args.image.read_bytes()
    if not 0x11000 <= len(image) <= 0x400000:
        raise SystemExit("firmware image size is outside the EchoDesk Mini limit")
    if image[0x1000] != 0xE9 or int.from_bytes(image[0x100C:0x100E], "little") != 0:
        raise SystemExit("firmware image does not target the original ESP32")
    if image[0x8000:0x8002] != b"\xaa\x50" or image[0x10000] != 0xE9:
        raise SystemExit("firmware image is not a valid merged EchoDesk Mini image")

    notes = args.release_notes or f"EchoDesk OS {version} for EchoDesk Mini."
    manifest = {
        "schemaVersion": 1,
        "generatedAt": dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "releases": [{
            "firmwareVersion": version,
            "hardwareModel": "EchoDesk Mini",
            "hardwareId": "freenove-fnk0114a-2p4",
            "supportedChip": "esp32",
            "imageType": "merged",
            "flashAddress": 0,
            "flashMode": "dio",
            "flashFrequency": "40m",
            "flashSize": "4MB",
            "imageFile": f"echodesk-mini/{version}/echodesk-os.bin",
            "downloadUrl": f"https://github.com/{REPOSITORY}/releases/download/{args.tag}/{expected_name}",
            "sha256": hashlib.sha256(image).hexdigest().upper(),
            "size": len(image),
            "releaseNotes": notes,
            "minimumDesktopVersion": args.minimum_desktop_version,
        }],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes((json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode("utf-8"))


if __name__ == "__main__":
    main()
