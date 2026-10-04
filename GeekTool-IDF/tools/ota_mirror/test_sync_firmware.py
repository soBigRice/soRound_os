import hashlib
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
from urllib.error import HTTPError

from sync_firmware import CHANNELS, MAX_IMAGE_SIZE, Mirror, MirrorError, NoRedirect, verify_image


def firmware(version="v1.6.1", project="GeekTool", extra=b""):
    header = bytearray(24)
    header[0:2] = bytes([0xE9, 1])
    struct.pack_into("<H", header, 12, 9)
    header[23] = 1
    descriptor = bytearray(256)
    struct.pack_into("<I", descriptor, 0, 0xABCD5432)
    descriptor[16:16 + len(version)] = version.encode("ascii")
    descriptor[48:48 + len(project)] = project.encode("ascii")
    payload = descriptor + extra
    data = header + struct.pack("<II", 0x3C000020, len(payload)) + payload
    checksum = 0xEF
    for byte in payload:
        checksum ^= byte
    data += bytes((15 - len(data)) % 16) + bytes([checksum])
    return bytes(data) + hashlib.sha256(data).digest()


class Response(io.BytesIO):
    status = 200

    def __init__(self, body, etag='"source-1"', size=None):
        super().__init__(body)
        self.headers = {"Content-Length": str(len(body) if size is None else size), "ETag": etag}


class Source:
    def __init__(self):
        self.images = {"GeekTool.bin": firmware(), "GeekTool-beta.bin": firmware("v1.7-beta.13")}
        self.etags = {name: '"source-1"' for name in self.images}
        self.get_failures = {}
        self.calls = []

    def open(self, request, timeout):
        name = request.full_url.split("?")[0].rsplit("/", 1)[-1]
        self.calls.append(request)
        data = self.images[name]
        if request.method == "HEAD":
            return Response(b"", self.etags[name], len(data))
        if name in self.get_failures:
            failure = self.get_failures[name]
            if isinstance(failure, Exception):
                raise failure
            return failure
        if request.headers.get("If-match") != self.etags[name]:
            raise HTTPError(request.full_url, 412, "changed object", {}, None)
        return Response(data, self.etags[name])


class MirrorTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.source = Source()
        self.mirror = Mirror("https://r2-ota.example.com", self.root / "public",
                             self.root / "state", opener=self.source)
        self.assertEqual(self.mirror.run(), 0)

    def tearDown(self):
        self.temporary.cleanup()

    def change(self, channel="beta"):
        name = CHANNELS[channel]
        self.source.images[name] = firmware("v1.7-beta.14" if channel == "beta" else "v1.6.2")
        self.source.etags[name] = '"source-2"'
        return name, self.root / "public" / name

    def test_verified_channels_and_no_download_when_unchanged(self):
        self.source.calls.clear()
        self.assertEqual(self.mirror.run(), 0)
        self.assertEqual([call.method for call in self.source.calls], ["HEAD", "HEAD"])
        state = json.loads((self.root / "state/beta.json").read_text())
        self.assertEqual(state["version"], "v1.7-beta.13")
        self.assertEqual(state["sha256"], hashlib.sha256(self.source.images["GeekTool-beta.bin"]).hexdigest())

    def test_atomic_replacement_preserves_inflight_reader_and_changes_etag_identity(self):
        name, path = self.change()
        old_mtime = int(path.stat().st_mtime)
        old_bytes = path.read_bytes()
        with path.open("rb") as inflight, patch("sync_firmware.time.time", return_value=old_mtime):
            self.assertEqual(self.mirror.run(), 0)
            self.assertEqual(inflight.read(), old_bytes)
        self.assertEqual(path.read_bytes(), self.source.images[name])
        self.assertGreater(int(path.stat().st_mtime), old_mtime)
        self.assertEqual(sorted(p.name for p in path.parent.iterdir()), sorted(CHANNELS.values()))

    def test_truncated_download_retains_image_and_state(self):
        name, path = self.change()
        old_bytes = path.read_bytes()
        old_state = (self.root / "state/beta.json").read_bytes()
        self.source.get_failures[name] = Response(self.source.images[name][:-20], '"source-2"', len(self.source.images[name]))
        self.assertEqual(self.mirror.run(), 1)
        self.assertEqual(path.read_bytes(), old_bytes)
        self.assertEqual((self.root / "state/beta.json").read_bytes(), old_state)
        self.assertFalse(list(path.parent.glob(".*")))

    def test_changed_source_etag_rejected(self):
        name, path = self.change()
        old_bytes = path.read_bytes()
        self.source.get_failures[name] = Response(self.source.images[name], '"source-3"')
        self.assertEqual(self.mirror.run(), 1)
        self.assertEqual(path.read_bytes(), old_bytes)

    def test_http_precondition_failure_retains_image(self):
        name, path = self.change()
        old_bytes = path.read_bytes()
        self.source.get_failures[name] = HTTPError("https://r2.example/bin", 412, "changed", {}, None)
        self.assertEqual(self.mirror.run(), 1)
        self.assertEqual(path.read_bytes(), old_bytes)

    def test_stable_failure_does_not_block_beta(self):
        stable, path = self.change("stable")
        beta, beta_path = self.change()
        old_stable = path.read_bytes()
        self.source.get_failures[stable] = OSError("offline")
        self.assertEqual(self.mirror.run(), 1)
        self.assertEqual(path.read_bytes(), old_stable)
        self.assertEqual(beta_path.read_bytes(), self.source.images[beta])

    def test_corruption_retains_previous_image(self):
        name, path = self.change()
        old_bytes = path.read_bytes()
        damaged = bytearray(self.source.images[name])
        damaged[-1] ^= 1
        self.source.images[name] = bytes(damaged)
        self.assertEqual(self.mirror.run(), 1)
        self.assertEqual(path.read_bytes(), old_bytes)

    def test_local_corruption_and_invalid_state_trigger_repair(self):
        path = self.root / "public/GeekTool-beta.bin"
        path.write_bytes(b"broken")
        (self.root / "state/beta.json").write_text("[]")
        self.assertEqual(self.mirror.run(), 0)
        self.assertEqual(path.read_bytes(), self.source.images[path.name])

    def test_download_deadline_retains_image(self):
        _, path = self.change()
        old_bytes = path.read_bytes()
        with patch("sync_firmware.time.monotonic", side_effect=[0, 181]):
            self.assertEqual(self.mirror.run(), 1)
        self.assertEqual(path.read_bytes(), old_bytes)

    def test_weak_etag_and_oversized_object_rejected(self):
        self.source.etags["GeekTool-beta.bin"] = 'W/"weak"'
        self.assertEqual(self.mirror.run(), 1)
        self.source.etags["GeekTool-beta.bin"] = '"valid"'
        self.source.images["GeekTool-beta.bin"] = b"x" * (MAX_IMAGE_SIZE + 1)
        self.assertEqual(self.mirror.run(), 1)

    def test_stable_never_accepts_beta_and_project_is_checked(self):
        with self.assertRaises(MirrorError):
            verify_image(firmware("v1.7-beta.13"), "stable")
        with self.assertRaises(MirrorError):
            verify_image(firmware(project="OtherApp"), "beta")
        self.assertEqual(verify_image(firmware("v1.6.1"), "beta")["version"], "v1.6.1")

    def test_source_cannot_loop_back_to_device_and_redirects_are_rejected(self):
        for url in ("https://ota.miaozong.cc", "https://OTA.MIAOZONG.CC./", "http://r2.example.com", "https://u:p@r2.example.com"):
            with self.assertRaises(MirrorError):
                Mirror(url, self.root / "public", self.root / "state")
        with self.assertRaises(MirrorError):
            NoRedirect().redirect_request(None, None, 302, "redirect", {}, "https://ota.miaozong.cc")


if __name__ == "__main__":
    unittest.main()
