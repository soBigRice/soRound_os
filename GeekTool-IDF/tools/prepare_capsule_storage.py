#!/usr/bin/env python3
"""Prepare local migration images only. This tool has no serial or flashing path.
仅生成本地迁移镜像和计划；不连接、擦除或烧录设备。
"""
import argparse
import hashlib
import importlib
import json
import os
from pathlib import Path
import struct
import sys
import zlib

STORAGE_OFFSET, STORAGE_SIZE = 0x1020000, 0x7E0000
META_OFFSET, CAPSULE_OFFSET, CAPSULE_SIZE = 0x1800000, 0x1801000, 0x7FF000


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def meta_image():
    header = b"GTVCAP01" + struct.pack("<5I", 1, CAPSULE_OFFSET, CAPSULE_SIZE, 4096, 2)
    header += struct.pack("<I", zlib.crc32(header, 0xFFFFFFFF))
    return header + b"\xff" * (4096 - len(header))


def prepare(assets, destination, idf):
    assets, destination, idf = Path(assets).resolve(), Path(destination).resolve(), Path(idf).resolve()
    if not assets.is_dir():
        raise ValueError("assets must be an explicitly selected complete backup directory")
    if destination.exists():
        raise ValueError("output directory already exists; refusing to overwrite a previous plan")
    if destination == assets or assets in destination.parents or destination in assets.parents:
        raise ValueError("source and output must be separate directories")
    sources = []
    for path in sorted(assets.rglob("*")):
        if path.is_symlink():
            raise ValueError("symlinked sources are not accepted")
        if path.is_file():
            sources.append({"path": str(path.relative_to(assets)), "bytes": path.stat().st_size, "sha256": sha(path)})
    if not sources:
        raise ValueError("asset backup is empty; refusing to produce an empty replacement")
    sys.path.insert(0, str(idf / "components/fatfs"))
    FATFS = importlib.import_module("fatfsgen").FATFS
    WLFATFS = importlib.import_module("wl_fatfsgen").WLFATFS
    # Use the pinned SDK generators, not an independently implemented filesystem.
    # 复用指定SDK生成器，不自行实现FAT或磨损均衡格式。
    fat = FATFS(size=STORAGE_SIZE, sector_size=4096, long_names_enabled=True)
    fat.generate(str(assets))
    wl = WLFATFS(size=CAPSULE_SIZE, sector_size=4096, long_names_enabled=True, device_id=0x47545643)
    wl.init_wl()
    # Recheck the source snapshot before writing the reviewable local result.
    # 写镜像前复核源文件快照，防止迁移计划与源数据不一致。
    for item in sources:
        path = assets / item["path"]
        if path.stat().st_size != item["bytes"] or sha(path) != item["sha256"]:
            raise ValueError("source changed while preparing images")
    if [str(p.relative_to(assets)) for p in sorted(assets.rglob("*")) if p.is_file()] != [item["path"] for item in sources]:
        raise ValueError("source file set changed while preparing images")
    destination.mkdir(parents=True, mode=0o700)
    fat.write_filesystem(str(destination / "storage.bin"))
    wl.wl_write_filesystem(str(destination / "capsules.bin"))
    (destination / "capsule_meta.bin").write_bytes(meta_image())
    images = []
    for name, offset, size in [("storage.bin", STORAGE_OFFSET, STORAGE_SIZE), ("capsule_meta.bin", META_OFFSET, 4096),
                               ("capsules.bin", CAPSULE_OFFSET, CAPSULE_SIZE)]:
        path = destination / name
        if path.stat().st_size != size:
            raise ValueError(f"unexpected generated size: {name}")
        path.chmod(0o600)
        images.append({"file": name, "offset": hex(offset), "bytes": size, "sha256": sha(path)})
    plan = {"schema": 1, "status": "PREPARED_ONLY_NOT_APPROVED_FOR_DEVICE_WRITE", "sdk": str(idf),
            "source": str(assets), "source_files": sources, "images": images,
            "required_before_migration": ["Explicit user approval before any device read/write or partition migration",
                "Back up the actual device partition table, full original storage and user files without overwriting them",
                "Verify actual device files match the selected complete backup; repository images alone are not proof",
                "Review partition table and image offsets; preserve NVS and both OTA application slots",
                "Initialize capsules first, then write capsule_meta last; verify read-back before enabling recording"],
            "warning": "Shrinking the existing raw FAT requires rebuilding and restoring all real files, not truncating the old partition."}
    (destination / "migration-plan.json").write_text(json.dumps(plan, ensure_ascii=False, indent=2) + "\n")
    return plan


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets-dir", required=True, help="Complete, explicitly selected asset backup directory")
    parser.add_argument("--output-dir", required=True, help="New local directory; never overwritten")
    parser.add_argument("--idf-path", default=os.environ.get("IDF_PATH"), required=not bool(os.environ.get("IDF_PATH")))
    args = parser.parse_args()
    plan = prepare(args.assets_dir, args.output_dir, args.idf_path)
    print(json.dumps({"status": plan["status"], "images": plan["images"]}, indent=2))


if __name__ == "__main__":
    main()
