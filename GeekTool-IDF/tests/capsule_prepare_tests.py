"""Offline SDK-image generation tests; all files live in a temporary directory."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

IDF = Path(sys.argv.pop(1)) if len(sys.argv) > 1 else None
spec = importlib.util.spec_from_file_location("capsule_prepare", Path(__file__).parents[1] / "tools/prepare_capsule_storage.py")
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class CapsulePreparationTests(unittest.TestCase):
    def test_guard_layout_and_crc(self):
        image = prepare.meta_image()
        self.assertEqual(len(image), 4096)
        self.assertEqual(image[:8], b"GTVCAP01")
        self.assertEqual(struct.unpack("<5I", image[8:28]), (1, 0x1801000, 0x7FF000, 4096, 2))
        self.assertEqual(struct.unpack("<I", image[28:32])[0], zlib.crc32(image[:28], 0xFFFFFFFF))
        self.assertEqual(image[32:], b"\xff" * (4096 - 32))

    def test_reject_empty_existing_and_nested_destination(self):
        with tempfile.TemporaryDirectory(prefix="gt-capsule-prepare-") as folder:
            root = Path(folder)
            source = root / "backup"
            source.mkdir()
            output = root / "images"
            with self.assertRaises(ValueError):
                prepare.prepare(source, output, IDF)
            (source / "bg.jpg").write_bytes(b"test backup")
            with self.assertRaises(ValueError):
                prepare.prepare(source, source / "images", IDF)
            output.mkdir()
            with self.assertRaises(ValueError):
                prepare.prepare(source, output, IDF)
            (source / "link").symlink_to(source / "bg.jpg")
            with self.assertRaises(ValueError):
                prepare.prepare(source, root / "new", IDF)

    def test_actual_sdk_images_and_source_fingerprints(self):
        self.assertIsNotNone(IDF, "Pass the explicit pinned ESP-IDF path")
        with tempfile.TemporaryDirectory(prefix="gt-capsule-prepare-") as folder:
            root = Path(folder)
            source = root / "backup"
            source.mkdir()
            contents = b"Preserve actual backup bytes\x00\xff"
            (source / "bg.jpg").write_bytes(contents)
            destination = root / "prepared"
            plan = prepare.prepare(source, destination, IDF)
            self.assertEqual(plan["status"], "PREPARED_ONLY_NOT_APPROVED_FOR_DEVICE_WRITE")
            self.assertEqual(plan["source_files"][0]["sha256"], hashlib.sha256(contents).hexdigest())
            self.assertEqual((source / "bg.jpg").read_bytes(), contents)
            self.assertEqual(len(plan["images"]), 3)
            self.assertEqual(json.loads((destination / "migration-plan.json").read_text()), plan)
            for image in plan["images"]:
                data = (destination / image["file"]).read_bytes()
                self.assertEqual(len(data), image["bytes"])
                self.assertEqual(hashlib.sha256(data).hexdigest(), image["sha256"])
            raw = (destination / "storage.bin").read_bytes()
            self.assertEqual(raw[510:512], b"\x55\xaa")
            self.assertIn(contents, raw)
            wl_image = (destination / "capsules.bin").read_bytes()
            sectors = len(wl_image) // 4096
            state_size = ((64 + sectors * 16 + 4095) // 4096) * 4096
            for copy in (1, 2):
                at = len(wl_image) - 4096 - copy * state_size
                state = wl_image[at:at + 64]
                self.assertEqual(struct.unpack("<I", state[24:28])[0], 2)
                self.assertEqual(struct.unpack("<I", state[60:64])[0], zlib.crc32(state[:60], 0xFFFFFFFF))


if __name__ == "__main__":
    unittest.main()
