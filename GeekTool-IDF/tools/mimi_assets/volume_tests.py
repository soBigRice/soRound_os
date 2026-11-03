#!/usr/bin/env python3
"""现场卷副本保全与错误路径检查；不连接设备。 / Snapshot preservation and failure checks; no device I/O."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
sys.dont_write_bytecode = True
from volume_tool import native_tool, inventory, append, run, FATView, preservation, digest


def expect_error(callback):
    try:
        callback()
    except (ValueError, RuntimeError, FileNotFoundError):
        return
    raise AssertionError("Invalid operation unexpectedly succeeded")


def check(volume: Path, sanitized: bool, keep_binary: Path | None) -> dict:
    original = volume.read_bytes();checked = []
    with native_tool(sanitize=sanitized) as (binary, audit):
        before = inventory(binary, volume)
        assert volume.read_bytes() == original
        checked.append("read_only_inventory")
        with tempfile.TemporaryDirectory(prefix="mimi-volume-check-") as directory:
            directory = Path(directory)
            # 采用完整素材预计分配量验证容量，不将测试字节误报为真实素材。
            # Exercise the full planned allocation; these test bytes are not real Mimi assets.
            pack = directory / "sizing-test.pak"
            size = 6_362_972
            pack.write_bytes((bytes(range(256)) * (size // 256 + 1))[:size])
            output = directory / "added.bin"
            proof, old_inventory, new_inventory = append(binary, volume, pack, output)
            assert proof["free_bytes_after"] == before["free_bytes"] - 6_365_184
            assert all(b["offset"] % 4096 == 0 for b in proof["write_order"])
            assert [b["class"] for b in proof["write_order"]] == sorted(
                [b["class"] for b in proof["write_order"]],
                key={"new_pack_data": 0, "fat": 1, "root_directory": 2}.__getitem__)
            checked.append("full_size_append_and_independent_file_cluster_fat_root_preservation")
            expect_error(lambda: append(binary, output, pack, directory / "duplicate.bin"))
            assert not (directory / "duplicate.bin").exists()
            expect_error(lambda: run(binary, "append", output, pack, directory / "native-duplicate.bin"))
            assert not (directory / "native-duplicate.bin").exists()
            checked.append("wrapper_and_native_CREATE_NEW_refuse_existing_pack")
            sentinel = directory / "sentinel.bin";sentinel.write_bytes(b"preserve existing output")
            expect_error(lambda: append(binary, volume, pack, sentinel))
            assert sentinel.read_bytes() == b"preserve existing output"
            expect_error(lambda: run(binary, "append", volume, pack, sentinel))
            assert sentinel.read_bytes() == b"preserve existing output"
            checked.append("wrapper_and_native_exclusive_output_preservation")
            too_large = directory / "too-large.pak"
            with too_large.open("wb") as file:file.truncate(before["free_bytes"] + 1)
            expect_error(lambda: append(binary, volume, too_large, directory / "overflow.bin"))
            assert not (directory / "overflow.bin").exists()
            expect_error(lambda: run(binary, "append", volume, too_large, directory / "native-overflow.bin"))
            assert not (directory / "native-overflow.bin").exists()
            checked.append("wrapper_and_native_insufficient_space_leave_no_output")
            short = directory / "short.bin";short.write_bytes(original[:1000])
            expect_error(lambda: run(binary, "inventory", short))
            malformed = bytearray(original);malformed[11:13] = b"\x00\x00"
            invalid = directory / "invalid.bin";invalid.write_bytes(malformed)
            expect_error(lambda: run(binary, "inventory", invalid))
            checked.append("invalid_size_and_geometry_rejected")
            expect_error(lambda: run(binary, "inventory", "/dev/null"))
            checked.append("non_regular_device_input_rejected")
            expect_error(lambda: run(binary, "append", volume, pack, volume))
            assert volume.read_bytes() == original
            checked.append("native_original_output_alias_refused")
            old = FATView(original)
            tampered = bytearray(output.read_bytes())
            if old.allocated:
                cluster = min(old.allocated)
                tampered[old.data_offset + (cluster - 2) * old.cluster_bytes] ^= 1
                expect_error(lambda: preservation(original, bytes(tampered), old_inventory, new_inventory, pack.read_bytes()))
            checked.append("independent_checker_detects_old_cluster_tampering")
            tampered = bytearray(output.read_bytes());tampered[old.fat_offset] ^= 1
            expect_error(lambda: preservation(original, bytes(tampered), old_inventory, new_inventory, pack.read_bytes()))
            checked.append("independent_checker_detects_reserved_fat_tampering")
            expect_error(lambda: append(binary, volume, directory / "missing.pak", directory / "missing.bin"))
            assert not (directory / "missing.bin").exists()
            checked.append("missing_pack_creates_no_output")
            assert volume.read_bytes() == original
            if keep_binary:
                if keep_binary.exists():raise ValueError("Binary output already exists")
                shutil.copy2(binary, keep_binary);keep_binary.chmod(0o700)
            return {"sanitized": sanitized, "checks": checked, "all_passed": True,
                    "original_unchanged": True, "fixture_volume_sha256": digest(original),
                    "full_size_test_pack_is_real_asset": False,
                    "full_size_test": {k: proof[k] for k in ("new_pack_size", "new_pack_sha256", "free_bytes_before", "free_bytes_after", "changed_4096_count", "old_file_count", "old_allocated_clusters")},
                    "native_tool": audit}


def main():
    os.umask(0o077)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--volume", type=Path, required=True)
    parser.add_argument("--proof", type=Path, required=True)
    parser.add_argument("--keep-binary", type=Path)
    args = parser.parse_args()
    results = [check(args.volume, False, args.keep_binary), check(args.volume, True, None)]
    proof = {"runs": results, "writes_device": False,
             "tests_source_sha256": digest(Path(__file__).read_bytes()),
             "volume_wrapper_sha256": digest(Path(__file__).with_name("volume_tool.py").read_bytes()),
             "temporary_images_and_objects_removed": True}
    args.proof.parent.mkdir(parents=True, exist_ok=True)
    args.proof.write_text(json.dumps(proof, indent=2) + "\n")
    print(json.dumps({"all_passed": True, "normal_checks": len(results[0]["checks"]), "asan_ubsan_checks": len(results[1]["checks"]),
                      "old_file_count": results[0]["full_size_test"]["old_file_count"],
                      "full_size_test_free_bytes_after": results[0]["full_size_test"]["free_bytes_after"],
                      "fixture_unchanged": all(r["original_unchanged"] for r in results)}))


if __name__ == "__main__":main()
