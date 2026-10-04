#!/usr/bin/env python3
"""Mirror the two public R2 OTA objects; publish only verified ESP32-S3 images."""

import argparse
import fcntl
import hashlib
import json
import logging
import os
from pathlib import Path
import struct
import tempfile
import time
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener


CHANNELS = {"stable": "GeekTool.bin", "beta": "GeekTool-beta.bin"}
MAX_IMAGE_SIZE = 0x400000  # Match the migrated ota_0 / ota_1 slots; guarded by the partition regression.
DEVICE_HOST = "ota.miaozong.cc"
LOG = logging.getLogger("ota-mirror")


class MirrorError(Exception):
    pass


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise MirrorError("R2 source redirected; configure its direct HTTPS hostname")


def verify_image(data, channel):
    """Check the IDF image framing, chip/project, XOR checksum and embedded SHA-256.

    HTTPS authenticates the public source. The embedded digest detects corruption;
    it does not add a firmware-signing contract to the existing unsigned releases.
    """
    if not 288 <= len(data) <= MAX_IMAGE_SIZE:
        raise MirrorError("image size is outside the OTA slot")
    if data[0] != 0xE9 or not 1 <= data[1] <= 16:
        raise MirrorError("invalid ESP image header")
    if struct.unpack_from("<H", data, 12)[0] != 9 or data[23] != 1:
        raise MirrorError("expected ESP32-S3 image with appended SHA-256")
    offset, checksum, descriptor = 24, 0xEF, None
    for segment in range(data[1]):
        if offset + 8 > len(data):
            raise MirrorError("truncated segment header")
        _, size = struct.unpack_from("<II", data, offset)
        offset += 8
        if offset + size > len(data):
            raise MirrorError("truncated segment data")
        payload = data[offset:offset + size]
        if segment == 0:
            descriptor = payload
        for byte in payload:
            checksum ^= byte
        offset += size
    # esptool places the checksum at the last byte of a 16-byte block.
    checksum_offset = offset + ((15 - offset) % 16)
    digest_offset = checksum_offset + 1
    if digest_offset + 32 != len(data):
        raise MirrorError("unexpected image footer or unsupported signed image")
    if any(data[offset:checksum_offset]) or data[checksum_offset] != checksum:
        raise MirrorError("invalid ESP image padding/checksum")
    if hashlib.sha256(data[:digest_offset]).digest() != data[digest_offset:]:
        raise MirrorError("embedded SHA-256 mismatch")
    if len(descriptor) < 256 or struct.unpack_from("<I", descriptor)[0] != 0xABCD5432:
        raise MirrorError("missing IDF application descriptor")
    try:
        version = descriptor[16:48].split(b"\0", 1)[0].decode("ascii")
        project = descriptor[48:80].split(b"\0", 1)[0].decode("ascii")
    except UnicodeDecodeError as exc:
        raise MirrorError("invalid application identity") from exc
    if project != "GeekTool" or not version.startswith("v"):
        raise MirrorError("unexpected project/version")
    if channel == "stable" and "-beta" in version:
        raise MirrorError("beta firmware cannot replace the stable channel")
    return {"version": version, "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest()}


def sync_directory(path):
    fd = os.open(path, os.O_RDONLY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def write_state(path, state):
    fd, temporary = tempfile.mkstemp(prefix=".state-", dir=path.parent)
    try:
        with os.fdopen(fd, "w") as output:
            json.dump(state, output, indent=2)
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
        sync_directory(path.parent)
    finally:
        Path(temporary).unlink(missing_ok=True)


class Mirror:
    def __init__(self, source_base, public_dir, state_dir, opener=None,
                 socket_timeout=15, download_timeout=180):
        source = urlsplit(source_base)
        if (source.scheme != "https" or not source.hostname or source.username
                or source.password or source.query or source.fragment
                or source.hostname.lower().rstrip(".") == DEVICE_HOST):
            raise MirrorError("source must be an independent public R2 HTTPS URL")
        self.source_base = source_base.rstrip("/")
        self.public_dir, self.state_dir = Path(public_dir), Path(state_dir)
        self.opener = opener or build_opener(NoRedirect())
        self.socket_timeout, self.download_timeout = socket_timeout, download_timeout

    def request(self, url, method="GET", etag=None):
        headers = {"Accept-Encoding": "identity", "Cache-Control": "no-cache",
                   "User-Agent": "soRound-OTA-Mirror/1"}
        if etag:
            headers["If-Match"] = etag
        return self.opener.open(Request(url, headers=headers, method=method),
                                timeout=self.socket_timeout)

    @staticmethod
    def metadata(response):
        if response.status != 200 or response.headers.get("Content-Encoding", "identity") != "identity":
            raise MirrorError("expected an uncompressed complete R2 object")
        try:
            size = int(response.headers["Content-Length"])
        except (KeyError, TypeError, ValueError) as exc:
            raise MirrorError("missing/invalid Content-Length") from exc
        etag = response.headers.get("ETag", "")
        if not (etag.startswith('"') and etag.endswith('"') and len(etag) > 2):
            raise MirrorError("R2 source must provide a strong ETag")
        if not 288 <= size <= MAX_IMAGE_SIZE:
            raise MirrorError("R2 object does not fit the OTA slot")
        return size, etag

    def sync_channel(self, channel):
        name = CHANNELS[channel]
        destination = self.public_dir / name
        state_path = self.state_dir / (channel + ".json")
        # A new query per poll avoids an old CDN object after an R2 overwrite.
        url = self.source_base + "/" + name + "?mirror=" + str(time.time_ns())
        with self.request(url, "HEAD") as head:
            size, etag = self.metadata(head)
        previous = {}
        if state_path.exists():
            try:
                previous = json.loads(state_path.read_text())
                if not isinstance(previous, dict):
                    raise ValueError("state must be an object")
            except (ValueError, OSError):
                previous = {}
                LOG.warning("%s: unreadable state; verifying source again", channel)
        if (previous.get("source_base") == self.source_base
                and previous.get("etag") == etag and destination.is_file()
                and destination.stat().st_size == size
                and hashlib.sha256(destination.read_bytes()).hexdigest() == previous.get("sha256")):
            LOG.info("%s: unchanged %s", channel, previous.get("version", "verified local image"))
            return False

        started = time.monotonic()
        fd, temporary = tempfile.mkstemp(prefix="." + name + "-", dir=self.public_dir)
        try:
            with os.fdopen(fd, "wb") as output, self.request(url, etag=etag) as response:
                if self.metadata(response) != (size, etag):
                    raise MirrorError("R2 object changed between HEAD and GET")
                received = 0
                while True:
                    if time.monotonic() - started > self.download_timeout:
                        raise MirrorError("download exceeded its deadline")
                    block = response.read(65536)
                    if not block:
                        break
                    received += len(block)
                    if received > size:
                        raise MirrorError("download exceeded Content-Length")
                    output.write(block)
                if received != size:
                    raise MirrorError("incomplete download")
                output.flush()
                os.fsync(output.fileno())
            image = verify_image(Path(temporary).read_bytes(), channel)
            os.chmod(temporary, 0o644)
            # nginx's static ETag uses mtime+size. Ensure changed bytes never
            # reuse the previous ETag, even for same-size builds in one second.
            old_mtime = int(destination.stat().st_mtime) if destination.exists() else 0
            modified = max(int(time.time()), old_mtime + 1)
            os.utime(temporary, (modified, modified))
            with open(temporary, "rb") as verified:
                os.fsync(verified.fileno())
            os.replace(temporary, destination)
            sync_directory(self.public_dir)
            write_state(state_path, dict(image, etag=etag, source_base=self.source_base,
                                         synced_at=int(time.time())))
            LOG.info("%s: published %s (%d bytes, sha256=%s)", channel,
                     image["version"], size, image["sha256"])
            return True
        finally:
            Path(temporary).unlink(missing_ok=True)

    def run(self):
        self.public_dir.mkdir(parents=True, exist_ok=True)
        self.state_dir.mkdir(parents=True, exist_ok=True)
        failures = 0
        with (self.state_dir / "sync.lock").open("a") as lock:
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                LOG.info("another sync is already running")
                return 0
            # Only our interrupted staging files; never remove a served image.
            for name in CHANNELS.values():
                for temporary in self.public_dir.glob("." + name + "-*"):
                    temporary.unlink()
            for channel in CHANNELS:
                try:
                    self.sync_channel(channel)
                except (MirrorError, OSError, ValueError) as exc:
                    failures += 1
                    LOG.error("%s: sync failed; no unverified image published: %s", channel, exc)
        return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-base", required=True)
    parser.add_argument("--public-dir", default="/srv/geektool-ota/public")
    parser.add_argument("--state-dir", default="/var/lib/geektool-ota")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    try:
        return Mirror(args.source_base, args.public_dir, args.state_dir).run()
    except (MirrorError, OSError) as exc:
        LOG.error("mirror cannot start: %s", exc)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
