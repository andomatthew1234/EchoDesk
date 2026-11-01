#!/usr/bin/env python3
"""Validate a signed firmware release before GitHub publication."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import tempfile
import urllib.parse


def fail(message: str) -> None:
    raise SystemExit(message)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", required=True, type=pathlib.Path)
    parser.add_argument("--manifest", required=True, type=pathlib.Path)
    parser.add_argument("--signature", required=True, type=pathlib.Path)
    parser.add_argument("--public-key", required=True, type=pathlib.Path)
    parser.add_argument("--tag", required=True)
    args = parser.parse_args()

    manifest_bytes = args.manifest.read_bytes()
    if len(manifest_bytes) > 128 * 1024:
        fail("manifest exceeds the 128 KiB limit")
    if len(args.signature.read_bytes()) != 64:
        fail("Ed25519 signature is not exactly 64 bytes")
    verified = subprocess.run([
        "openssl", "pkeyutl", "-verify", "-rawin", "-pubin",
        "-inkey", str(args.public_key), "-sigfile", str(args.signature),
        "-in", str(args.manifest),
    ], check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if verified.returncode != 0:
        fail("manifest signature verification failed")

    try:
        manifest = json.loads(manifest_bytes)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        fail(f"manifest is invalid JSON: {error}")
    if set(manifest) != {"schemaVersion", "generatedAt", "releases"} or manifest["schemaVersion"] != 1:
        fail("unsupported manifest envelope")
    if not isinstance(manifest["releases"], list) or len(manifest["releases"]) != 1:
        fail("release manifest must contain exactly one release")
    release = manifest["releases"][0]
    version = args.tag.removeprefix("fw-beta-v").removeprefix("fw-v")
    expected_name = f"echodesk-os-freenove-fnk0114a-2p4-v{version}.bin"
    if release.get("firmwareVersion") != version or args.image.name != expected_name:
        fail("tag, manifest, and firmware filename versions disagree")
    required = {
        "hardwareModel": "EchoDesk Mini", "hardwareId": "freenove-fnk0114a-2p4",
        "supportedChip": "esp32", "imageType": "merged", "flashAddress": 0,
        "flashMode": "dio", "flashFrequency": "40m", "flashSize": "4MB",
    }
    if any(release.get(key) != value for key, value in required.items()):
        fail("manifest does not contain the EchoDesk Mini flash allowlist")
    url = urllib.parse.urlparse(str(release.get("downloadUrl", "")))
    expected_path = f"/andomatthew1234/EchoDesk/releases/download/{args.tag}/{expected_name}"
    if url.scheme != "https" or url.netloc != "github.com" or url.path != expected_path:
        fail("download URL is not confined to the matching EchoDesk release tag")

    image = args.image.read_bytes()
    if release.get("size") != len(image) or release.get("sha256", "").upper() != hashlib.sha256(image).hexdigest().upper():
        fail("firmware size or digest does not match the signed manifest")
    if not 0x11000 <= len(image) <= 0x400000:
        fail("firmware image size is invalid")
    if image[0x1000] != 0xE9 or int.from_bytes(image[0x100C:0x100E], "little") != 0:
        fail("firmware bootloader is not original ESP32")
    if image[0x8000:0x8002] != b"\xaa\x50" or image[0x10000] != 0xE9:
        fail("firmware merged-image layout is invalid")

    with tempfile.TemporaryDirectory() as directory:
        tampered = pathlib.Path(directory) / "manifest.json"
        tampered.write_bytes(manifest_bytes + b" ")
        rejected = subprocess.run([
            "openssl", "pkeyutl", "-verify", "-rawin", "-pubin",
            "-inkey", str(args.public_key), "-sigfile", str(args.signature),
            "-in", str(tampered),
        ], check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if rejected.returncode == 0:
            fail("tampered manifest unexpectedly passed signature verification")
    print(f"PASS: signed EchoDesk OS {version} release contract is valid")


if __name__ == "__main__":
    main()
