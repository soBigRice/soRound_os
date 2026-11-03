#!/usr/bin/env python3
"""只在离线副本追加MIMI.PAK并证明原文件/簇保持。 / Append to offline copies and prove preservation."""
from __future__ import annotations

import argparse
import contextlib
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

VOLUME_BYTES = 0x7E0000
STORAGE_OFFSET = 0x1020000
TOOLS = Path(__file__).resolve().parent


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


@contextlib.contextmanager
def native_tool(idf: Path | None = None, sanitize: bool = False):
    """SDK原件仅复制到临时目录，不改SDK或项目配置。 / Copy SDK originals; never edit SDK/project configuration."""
    idf = (idf or Path(os.environ.get("IDF_PATH", Path.home() / ".espressif/v6.0.1/esp-idf"))).resolve()
    sources = idf / "components/fatfs/src"
    if "R0.15 w/patch2" not in (sources / "ff.c").read_text():
        raise ValueError("Expected the reviewed ESP-IDF6.0.1 FatFs R0.15/patch2")
    names = ("ff.c", "ff.h", "diskio.h", "ffunicode.c")
    audit = {"sdk": str(idf), "sdk_sources": {n: digest((sources / n).read_bytes()) for n in names},
             "host_source_sha256": digest((TOOLS / "volume_tool.c").read_bytes()),
             "host_ffconf_sha256": digest((TOOLS / "volume_ffconf.h").read_bytes()),
             "formatting_compiled_out": True, "sanitized": sanitize}
    with tempfile.TemporaryDirectory(prefix="mimi-fatfs-") as directory:
        directory = Path(directory)
        for name in names:
            shutil.copy2(sources / name, directory / name)
        shutil.copy2(TOOLS / "volume_ffconf.h", directory / "ffconf.h")
        shutil.copy2(TOOLS / "volume_tool.c", directory / "volume_tool.c")
        command = ["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                   "-I", str(directory), str(directory / "volume_tool.c"), str(directory / "ff.c"),
                   str(directory / "ffunicode.c"), "-o", str(directory / "volume_tool")]
        if sanitize:
            command[2:3] = ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, check=True, capture_output=True)
        yield directory / "volume_tool", audit


def run(binary: Path, *args: str | Path) -> bytes:
    result = subprocess.run([str(binary), *(str(a) for a in args)], capture_output=True)
    if result.returncode:
        raise RuntimeError(result.stderr.decode(errors="replace").strip() or f"FatFs helper failed: {result.returncode}")
    return result.stdout


def inventory(binary: Path, image: Path) -> dict:
    before = digest(image.read_bytes())
    result = json.loads(run(binary, "inventory", image))
    for item in result["files"]:
        if not item["directory"]:
            data = run(binary, "cat", image, item["path"])
            if len(data) != item["size"]:
                raise ValueError("FatFs inventory/read size mismatch")
            item["sha256"] = digest(data)
    if before != digest(image.read_bytes()):
        raise ValueError("Read-only inventory unexpectedly modified its input")
    result["volume_sha256"] = before
    return result


class FATView:
    """独立只读解析用于核对，所有写入由SDK FatFs完成。 / Independent read-only checks; only SDK FatFs writes."""
    def __init__(self, data: bytes):
        if len(data) != VOLUME_BYTES:
            raise ValueError("Unexpected resource partition size")
        self.data = data
        self.sector = struct.unpack_from("<H", data, 11)[0]
        self.cluster_sectors = data[13]
        self.reserved = struct.unpack_from("<H", data, 14)[0]
        self.fats = data[16]
        self.root_entries = struct.unpack_from("<H", data, 17)[0]
        self.total_sectors = struct.unpack_from("<H", data, 19)[0] or struct.unpack_from("<I", data, 32)[0]
        self.fat_sectors = struct.unpack_from("<H", data, 22)[0]
        if self.sector not in (512, 1024, 2048, 4096) or self.cluster_sectors not in (1, 2, 4, 8, 16, 32, 64, 128):
            raise ValueError("Invalid FAT sector/cluster geometry")
        if not self.reserved or self.fats not in (1, 2) or not self.fat_sectors or not self.root_entries:
            raise ValueError("Only existing FAT12/FAT16 resource volumes are accepted")
        if self.total_sectors * self.sector != len(data):
            raise ValueError("FAT volume/partition size mismatch")
        if data[510:512] != b"\x55\xaa":
            raise ValueError("Existing FAT boot signature is invalid")
        self.root_sectors = (self.root_entries * 32 + self.sector - 1) // self.sector
        self.fat_offset = self.reserved * self.sector
        self.root_offset = (self.reserved + self.fats * self.fat_sectors) * self.sector
        self.data_offset = self.root_offset + self.root_sectors * self.sector
        self.cluster_bytes = self.cluster_sectors * self.sector
        self.cluster_count = (len(data) - self.data_offset) // self.cluster_bytes
        self.bits = 12 if self.cluster_count < 4085 else 16 if self.cluster_count < 65525 else 32
        if self.bits not in (12, 16):
            raise ValueError("FAT32/exFAT are outside this resource tool's contract")
        self.entries = [self.entry(n) for n in range(self.cluster_count + 2)]
        self.allocated = {n for n in range(2, self.cluster_count + 2) if self.entries[n] != 0}
        for copy in range(1, self.fats):
            first = data[self.fat_offset:self.fat_offset + self.fat_sectors * self.sector]
            other = data[self.fat_offset + copy * self.fat_sectors * self.sector:self.fat_offset + (copy + 1) * self.fat_sectors * self.sector]
            if first != other:
                raise ValueError("Existing FAT mirrors disagree")

    def entry(self, cluster: int) -> int:
        if self.bits == 12:
            at = self.fat_offset + cluster + cluster // 2
            value = struct.unpack_from("<H", self.data, at)[0]
            return (value >> 4 if cluster & 1 else value) & 0xFFF
        return struct.unpack_from("<H", self.data, self.fat_offset + cluster * 2)[0]

    def cluster_data(self, cluster: int) -> bytes:
        if not 2 <= cluster < self.cluster_count + 2:
            raise ValueError("Cluster outside data region")
        at = self.data_offset + (cluster - 2) * self.cluster_bytes
        return self.data[at:at + self.cluster_bytes]

    def chain(self, first: int, size: int) -> list[int]:
        if not first and not size:
            return []
        result, seen = [], set()
        end = 0xFF8 if self.bits == 12 else 0xFFF8
        value = first
        while value < end:
            if value in seen or not 2 <= value < self.cluster_count + 2:
                raise ValueError("Invalid/looped file cluster chain")
            seen.add(value)
            result.append(value)
            value = self.entries[value]
        expected = (size + self.cluster_bytes - 1) // self.cluster_bytes
        if len(result) != expected:
            raise ValueError("File cluster chain length disagrees with file size")
        return result


def changed_blocks(before: bytes, after: bytes, size: int) -> list[int]:
    return [at for at in range(0, len(before), size) if before[at:at + size] != after[at:at + size]]


def preservation(before: bytes, after: bytes, old_inventory: dict, new_inventory: dict, pack: bytes) -> dict:
    old, new = FATView(before), FATView(after)
    if before[:old.fat_offset] != after[:old.fat_offset]:
        raise ValueError("Boot sector/reserved data changed")
    geometry = ("sector", "cluster_sectors", "reserved", "fats", "root_entries", "total_sectors", "fat_sectors", "data_offset")
    if any(getattr(old, n) != getattr(new, n) for n in geometry):
        raise ValueError("Resource volume geometry changed")
    old_files = {f["path"]: f for f in old_inventory["files"]}
    new_files = {f["path"]: f for f in new_inventory["files"]}
    if set(new_files) != set(old_files) | {"/MIMI.PAK"}:
        raise ValueError("Append changed the existing file catalog")
    if any(new_files[name] != metadata for name, metadata in old_files.items()):
        raise ValueError("Existing file metadata/content/first cluster changed")
    for cluster in old.allocated:
        if old.entries[cluster] != new.entries[cluster] or old.cluster_data(cluster) != new.cluster_data(cluster):
            raise ValueError("An existing allocated cluster or chain changed")
    if old.entries[:2] != new.entries[:2]:
        raise ValueError("Reserved FAT entries changed")
    # 原目录活动项及LFN项必须逐字节保留；根目录旧结束标记之后才可新增。
    # Preserve every active old directory/LFN entry byte-for-byte; append only into old free slots.
    root_end = old.root_offset + old.root_sectors * old.sector
    past_end = False
    for at in range(old.root_offset, root_end, 32):
        marker = before[at]
        past_end = past_end or marker == 0
        if not past_end and marker != 0xE5 and before[at:at + 32] != after[at:at + 32]:
            raise ValueError("An existing root directory entry changed")
    mimi = new_files["/MIMI.PAK"]
    if mimi["directory"] or mimi["size"] != len(pack) or mimi["sha256"] != digest(pack):
        raise ValueError("New pack readback hash/size mismatch")
    chain = new.chain(mimi["first_cluster"], len(pack))
    if set(chain) & old.allocated:
        raise ValueError("New pack overlaps existing data clusters")
    if new.allocated - old.allocated != set(chain):
        raise ValueError("Append allocated data outside the new pack chain")
    for cluster in range(2, old.cluster_count + 2):
        if old.cluster_data(cluster) != new.cluster_data(cluster) and cluster not in chain:
            raise ValueError("Data outside the new pack clusters changed")
    data_end = old.data_offset + old.cluster_count * old.cluster_bytes
    if before[data_end:] != after[data_end:]:
        raise ValueError("Unused volume tail changed")
    changes = changed_blocks(before, after, 4096)
    blocks = []
    for offset in changes:
        if offset < old.root_offset:
            classification = "fat"
        elif offset < old.data_offset:
            classification = "root_directory"
        else:
            classification = "new_pack_data"
        blocks.append({"offset": offset, "absolute_flash_offset": STORAGE_OFFSET + offset,
                       "bytes": 4096, "class": classification,
                       "before_sha256": digest(before[offset:offset + 4096]),
                       "after_sha256": digest(after[offset:offset + 4096])})
    order = {"new_pack_data": 0, "fat": 1, "root_directory": 2}
    blocks.sort(key=lambda b: (order[b["class"]], b["offset"]))
    return {"old_file_count": len(old_files), "old_files_unchanged": True,
            "old_root_entries_byte_identical": True, "old_allocated_clusters": len(old.allocated),
            "old_allocated_cluster_bytes_identical": True, "old_allocated_fat_entries_identical": True,
            "new_pack_clusters": len(chain), "new_pack_size": len(pack), "new_pack_sha256": digest(pack),
            "volume_before_sha256": digest(before), "volume_after_sha256": digest(after),
            "volume_before_md5": hashlib.md5(before).hexdigest(), "volume_after_md5": hashlib.md5(after).hexdigest(),
            "geometry": {n: getattr(old, n) for n in geometry} | {"fat_bits": old.bits},
            "free_bytes_before": old_inventory["free_bytes"], "free_bytes_after": new_inventory["free_bytes"],
            "changed_512_offsets": changed_blocks(before, after, 512),
            "changed_4096_count": len(changes), "write_order": blocks,
            "write_order_note": "New pack data first, FAT next, root directory last; not an atomic/power-failure-safe transaction",
            "writes_device": False, "formatting_used": False}


def append(binary: Path, old: Path, pack_path: Path, output: Path) -> tuple[dict, dict, dict]:
    if output.exists() or old.resolve() == output.resolve() or pack_path.resolve() == output.resolve():
        raise ValueError("A new distinct output image is required; overwriting is forbidden")
    before, pack = old.read_bytes(), pack_path.read_bytes()
    old_inventory = inventory(binary, old)
    if any(f["path"].casefold() == "/mimi.pak" for f in old_inventory["files"]):
        raise ValueError("MIMI.PAK already exists; CREATE_NEW refuses replacement")
    if len(pack) > old_inventory["free_bytes"]:
        raise ValueError("Pack exceeds current free space")
    run(binary, "append", old, pack_path, output)
    try:
        after = output.read_bytes()
        if old.read_bytes() != before:
            raise ValueError("The original volume changed")
        new_inventory = inventory(binary, output)
        proof = preservation(before, after, old_inventory, new_inventory, pack)
        return proof, old_inventory, new_inventory
    except BaseException:
        output.unlink(missing_ok=True)
        raise


def main():
    os.umask(0o077)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--idf", type=Path)
    sub = parser.add_subparsers(dest="command", required=True)
    info = sub.add_parser("inventory");info.add_argument("image", type=Path);info.add_argument("--json", type=Path, required=True)
    add = sub.add_parser("append");add.add_argument("old", type=Path);add.add_argument("pack", type=Path);add.add_argument("output", type=Path);add.add_argument("--proof", type=Path, required=True)
    args = parser.parse_args()
    with native_tool(args.idf) as (binary, audit):
        if args.command == "inventory":
            result = inventory(binary, args.image);result["native_tool"] = audit
            args.json.parent.mkdir(parents=True, exist_ok=True);args.json.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n")
            print(json.dumps({"files": len(result["files"]), "free_bytes": result["free_bytes"], "volume_sha256": result["volume_sha256"]}))
        else:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            proof, old_inventory, new_inventory = append(binary, args.old, args.pack, args.output)
            proof["native_tool"] = audit
            args.proof.parent.mkdir(parents=True, exist_ok=True)
            args.proof.write_text(json.dumps(proof, indent=2) + "\n")
            for suffix, contents in (("old-files", old_inventory), ("new-files", new_inventory)):
                args.proof.with_name(args.proof.stem + "-" + suffix + ".json").write_text(json.dumps(contents, indent=2, ensure_ascii=False) + "\n")
            print(json.dumps({k: proof[k] for k in ("old_file_count", "old_files_unchanged", "new_pack_size", "new_pack_sha256", "changed_4096_count", "free_bytes_after")}))


if __name__ == "__main__":
    main()
