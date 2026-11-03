#!/usr/bin/env python3
"""核对实际C读取器、全部原生流及损坏输入。 / Check the actual C reader, native streams and malformed inputs."""
from pathlib import Path
import argparse
import ctypes as c
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
import threading
import zlib

sys.dont_write_bytecode = True
from build_pack import HEADER, INDEX, STREAMS, deflate, header, rle

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
SDK = Path(os.environ.get("IDF_PATH", Path.home() / ".espressif/v6.0.1/esp-idf"))


class BodyStart(c.Structure):
    _fields_ = [("offset", c.c_uint16), ("skip", c.c_uint16)]


class Frame(c.Structure):
    _fields_ = [("index", c.c_uint32), ("clip", c.c_uint32), ("pose_ms", c.c_uint32),
                ("base_rows", c.POINTER(c.c_uint32)), ("span_rows", c.POINTER(c.c_uint32)),
                ("body_rle", c.POINTER(c.c_uint8)), ("base_rle", c.POINTER(c.c_uint8)),
                ("span_data", c.POINTER(c.c_uint8)), ("body_bytes", c.c_uint32),
                ("base_bytes", c.c_uint32), ("span_count", c.c_uint32), ("body_rows", c.POINTER(BodyStart))]


class Slot(c.Structure):
    _fields_ = [("frame", Frame), ("base_rows", c.c_uint32 * 467), ("span_rows", c.c_uint32 * 467),
                ("body_rows", BodyStart * 466), ("data", c.c_uint8 * 416800)]


class Guard(c.Structure):
    _fields_ = [("before", c.c_uint64 * 2), ("slot", Slot), ("after", c.c_uint64 * 2)]


def configure(lib):
    lib.mimi_assets_open.argtypes, lib.mimi_assets_open.restype = [c.c_char_p], c.c_void_p
    lib.mimi_assets_close.argtypes = [c.c_void_p]
    lib.mimi_assets_find.argtypes = [c.c_void_p, c.c_uint, c.c_uint32, c.POINTER(c.c_uint), c.POINTER(c.c_uint32)]
    lib.mimi_assets_find.restype = c.c_bool
    lib.mimi_assets_load.argtypes, lib.mimi_assets_load.restype = [c.c_void_p, c.c_uint, c.POINTER(Slot)], c.c_bool
    lib.mimi_assets_body_row.argtypes, lib.mimi_assets_body_row.restype = [c.POINTER(Frame), c.c_uint, c.POINTER(c.c_uint8)], c.c_bool
    lib.assets_probe_fail.argtypes = [c.c_uint]
    lib.assets_probe_slot_size.restype, lib.assets_probe_frame_size.restype = c.c_size_t, c.c_size_t
    assert lib.assets_probe_slot_size() == c.sizeof(Slot) and lib.assets_probe_frame_size() == c.sizeof(Frame)
    return lib


def no_leaks(lib):
    assert lib.assets_probe_live() == 0 and lib.assets_probe_files() == 0


def guards(g):
    assert list(g.before) == [0x123456789ABCDEF0] * 2
    assert list(g.after) == [0xFEDCBA9876543210] * 2


def fix_metadata(data):
    struct.pack_into("<I", data, 32, len(data))
    struct.pack_into("<I", data, 36, zlib.crc32(data[64:64 + 186 * 64]))
    struct.pack_into("<I", data, 60, zlib.crc32(data[:60]))
    return data


def toy_pack(change=None):
    base_rows = struct.pack("<467I", *(y * 4 for y in range(467)))
    span_rows = struct.pack("<467I", 0, *([1] * 466))
    body = rle(bytes([255]) * (466 * 466), 1)
    base = (struct.pack("<HH", 0x8000 | 466, 0xA342)) * 466
    spans = struct.pack("<HHHBB", 0, 1, 0x3478, 32, 0)
    baseline = [base_rows, span_rows, body, base, spans]
    records, payload = bytearray(), bytearray()
    for frame in range(186):
        raw = list(baseline)
        if frame == 0 and change:
            raw = change(raw)
        packed = [deflate(x) for x in raw]
        if frame == 0 and getattr(change, "trailing", False):
            packed[0] += b"x"
        if frame == 0 and getattr(change, "underflow", False):
            packed[2] = deflate(raw[2][:-1])
        if frame == 0 and getattr(change, "overflow", False):
            packed[2] = deflate(raw[2] + b"x")
        first, clip = (0, 10) if frame < 50 else ((50, 11) if frame < 128 else (128, 13))
        crc = 0
        for item in raw:
            crc = zlib.crc32(item, crc)
        records += INDEX.pack(64 + 186 * 64 + len(payload), (frame - first) * 50, clip, 0,
                              *map(len, raw), *map(len, packed), crc, bytes(12))
        payload += b"".join(packed)
        payload += bytes((-len(payload)) % 4)
    return header(64 + len(records) + len(payload), records) + records + payload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", type=Path, default=PROJECT / "build/flash-records/mimi-full-20261103/assets")
    parser.add_argument("--tinfl-source", type=Path, help="Optional public-domain miniz v114 source for the ESP wrapper host check (not a ROM test)")
    args = parser.parse_args()
    directory = args.assets.resolve()
    pack, manifest = directory / "MIMI.PAK", json.loads((directory / "manifest.json").read_text())
    original = pack.read_bytes()
    assert hashlib.sha256(original).hexdigest() == manifest["sha256"] and len(original) == manifest["size"]
    probe = PROJECT / "tests/mimi/asset_probe.c"
    common = ["/usr/bin/cc", "-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-Werror", "-I" + str(PROJECT / "main")]
    results = {}
    with tempfile.TemporaryDirectory(prefix="reader-check-", dir=directory) as temp_name:
        temp = Path(temp_name)
        shared = temp / ("reader.dylib" if sys.platform == "darwin" else "reader.so")
        subprocess.run(common + (["-dynamiclib"] if sys.platform == "darwin" else ["-shared", "-fPIC"]) + [str(probe), "-lz", "-o", str(shared)], check=True)
        lib = configure(c.CDLL(str(shared)))
        for fail in (1, 2):
            lib.assets_probe_fail(fail)
            assert not lib.mimi_assets_open(os.fsencode(pack))
            no_leaks(lib)
        lib.assets_probe_fail(0)
        a = lib.mimi_assets_open(os.fsencode(pack))
        assert a
        g = Guard()
        g.before[:] = [0x123456789ABCDEF0] * 2
        g.after[:] = [0xFEDCBA9876543210] * 2
        row = (c.c_uint8 * 468)()
        row[0] = row[467] = 0xA7
        lookup_error = []

        def lookup():
            for n in range(5000):
                clip, start, count = ((10, 0, 50), (11, 50, 78), (13, 128, 58))[n % 3]
                ms = n * 17
                index, pose = c.c_uint(), c.c_uint32()
                if not lib.mimi_assets_find(a, clip, ms, c.byref(index), c.byref(pose)) or index.value != start + min(ms // 50, count - 1) or pose.value != min(ms // 50, count - 1) * 50:
                    lookup_error.append(n)
                    break

        thread = threading.Thread(target=lookup)
        thread.start()
        body_rows = 0
        for meta in manifest["frames"]:
            assert lib.mimi_assets_load(a, meta["frame"], c.byref(g.slot)), meta["frame"]
            f = g.slot.frame
            assert (f.index, f.clip, f.pose_ms) == (meta["frame"], meta["clip"], meta["ms"])
            ptrs = (f.base_rows, f.span_rows, f.body_rle, f.base_rle, f.span_data)
            for key, ptr, length in zip(STREAMS, ptrs, meta["raw_sizes"]):
                assert hashlib.sha256(c.string_at(ptr, length)).hexdigest() == meta["raw_sha256"][key], (meta["frame"], key)
            digest = hashlib.sha256()
            for y in range(466):
                assert lib.mimi_assets_body_row(c.byref(f), y, c.cast(c.byref(row, 1), c.POINTER(c.c_uint8)))
                assert row[0] == row[467] == 0xA7
                digest.update(bytes(row)[1:467])
                body_rows += 1
            assert digest.hexdigest() == meta["body_sha256"], meta["frame"]
            assert lib.assets_probe_calls() == 2 and lib.assets_probe_live() == 2 and lib.assets_probe_files() == 1
            guards(g)
        thread.join()
        assert not lookup_error
        lib.mimi_assets_close(a)
        no_leaks(lib)
        results["native"] = {"frames": 186, "raw_stream_sha256_checks": 930, "body_rows": body_rows,
                             "body_sha256_checks": 186, "concurrent_lookups": 5000, "slot_canaries": True, "allocation_failures": 2}
        variant = temp / "variant.pak"
        rejected = []

        def reject(name, data, load=False, frame=0):
            variant.write_bytes(data)
            lib.assets_probe_fail(0)
            value = lib.mimi_assets_open(os.fsencode(variant))
            if load:
                assert value, name
                assert not lib.mimi_assets_load(value, frame, c.byref(g.slot)), name
                lib.mimi_assets_close(value)
            else:
                assert not value, name
            guards(g)
            no_leaks(lib)
            rejected.append(name)

        for name, offset in (("magic", 0), ("version", 8), ("dimensions", 20), ("header_crc", 60)):
            data = bytearray(original)
            data[offset] ^= 1
            reject(name, data)
        for name, offset, value, fmt in (("header_reserved", 44, 1, "<B"), ("index_reserved", 64 + 52, 1, "<B"),
                                        ("wrong_clip", 64 + 6, 11, "<B"), ("wrong_time", 64 + 4, 50, "<H"),
                                        ("raw_overflow", 64 + 8 + 3 * 4, 0xFFFFFFFF, "<I"),
                                        ("packed_overflow", 64 + 28, 0xFFFFFFFF, "<I"),
                                        ("wrong_table_size", 64 + 8, 4, "<I"), ("span_size", 64 + 8 + 4 * 4, 7, "<I")):
            data = bytearray(original)
            struct.pack_into(fmt, data, offset, value)
            reject(name, fix_metadata(data))
        reject("truncated_file", original[:-1])
        reject("trailing_file_data", fix_metadata(bytearray(original + b"x")))
        data = bytearray(original)
        struct.pack_into("<I", data, 64 + 48, struct.unpack_from("<I", data, 64 + 48)[0] ^ 1)
        reject("raw_crc", fix_metadata(data), load=True)
        data = bytearray(original)
        first = manifest["frames"][0]
        offset = first["offset"] + sum(first["packed_sizes"][:2])
        data[offset + first["packed_sizes"][2] // 2] ^= 8
        reject("corrupt_compressed_body", data, load=True)
        for meta in manifest["frames"]:
            end = meta["offset"] + sum(meta["packed_sizes"])
            if end % 4:
                data = bytearray(original)
                data[end] = 1
                reject("nonzero_frame_padding", data, load=True, frame=meta["frame"])
                break
        variant.write_bytes(original)
        a = lib.mimi_assets_open(os.fsencode(variant))
        assert a
        os.truncate(variant, manifest["frames"][0]["offset"] + 4)
        assert not lib.mimi_assets_load(a, 0, c.byref(g.slot))
        lib.mimi_assets_close(a)
        no_leaks(lib)
        rejected.append("truncated_after_open")
        toy = toy_pack()
        variant.write_bytes(toy)
        a = lib.mimi_assets_open(os.fsencode(variant))
        assert a and lib.mimi_assets_load(a, 0, c.byref(g.slot))
        lib.mimi_assets_close(a)

        def changed(stream, position, replacement):
            def apply(raw):
                data = bytearray(raw[stream])
                data[position:position + len(replacement)] = replacement
                raw[stream] = bytes(data)
                return raw
            return apply

        for name, change in (("rle_body_zero_count", changed(2, 0, b"\0\0")),
                             ("rle_base_zero_count", changed(3, 0, b"\0\0")),
                             ("span_zero_count", changed(4, 2, b"\0\0")),
                             ("span_bad_x", changed(4, 0, struct.pack("<H", 466))),
                             ("span_bad_q", changed(4, 6, b"!")),
                             ("span_bad_padding", changed(4, 7, b"x")),
                             ("nonmonotonic_rows", changed(1, 4, struct.pack("<I", 2)))):
            reject(name, toy_pack(change), load=True)
        for name in ("trailing", "underflow", "overflow"):
            change = lambda raw: raw
            setattr(change, name, True)
            reject("deflate_" + name, toy_pack(change), load=True)
        results["malformed"] = {"rejected": rejected, "count": len(rejected), "heap_and_files_after_each": 0, "slot_canaries": True}
        expected = temp / "expected.h"
        expected.write_text("static const unsigned expected_body_crc[186]={" + ",".join(str(x["body_crc32"]) + "u" for x in manifest["frames"]) + "};\n")
        native = temp / "asset-sanitized"
        command = common + ["-O1", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-DMIMI_ASSET_PROBE_MAIN",
                            '-DASSET_EXPECTED_HEADER="' + str(expected) + '"', str(probe), "-lz", "-o", str(native)]
        subprocess.run(command, check=True)
        sanitized = subprocess.run([str(native), str(pack)], check=True, text=True, capture_output=True)
        results["asan_ubsan"] = sanitized.stdout.strip()
        esp_shared = temp / ("esp-reader.dylib" if sys.platform == "darwin" else "esp-reader.so")
        esp_flags = ["-DESP_PLATFORM", "-Wno-macro-redefined", "-I" + str(PROJECT / "tests/mimi/asset_stubs"),
                     "-I" + str(SDK / "components/esp_rom/include"), "-I" + str(SDK / "components/esp_rom/esp32s3")]
        subprocess.run(common + esp_flags + (["-dynamiclib"] if sys.platform == "darwin" else ["-shared", "-fPIC"]) + [str(probe), "-lz", "-o", str(esp_shared)], check=True)
        esp = configure(c.CDLL(str(esp_shared)))
        for fail in (1, 2, 3):
            esp.assets_probe_fail(fail)
            assert not esp.mimi_assets_open(os.fsencode(pack))
            no_leaks(esp)
        esp.assets_probe_fail(0)
        a = esp.mimi_assets_open(os.fsencode(pack))
        assert a and esp.assets_probe_live() == 3
        esp.mimi_assets_close(a)
        no_leaks(esp)
        results["esp_allocation_paths"] = {"failures_checked": 3, "rom_inflater_executed": False}
        if args.tinfl_source:
            tinfl = temp / "tinfl.o"
            subprocess.run(["/usr/bin/cc", "-O2", "-DMINIZ_NO_ZLIB_APIS", "-DMINIZ_NO_ARCHIVE_APIS", "-DMINIZ_NO_STDIO",
                            "-c", str(args.tinfl_source.resolve()), "-o", str(tinfl)], check=True, capture_output=True)
            executable = temp / "esp-streaming-reader"
            command = common + esp_flags + ["-DMIMI_ASSET_REAL_TINFL", "-DMIMI_ASSET_PROBE_MAIN",
                       '-DASSET_EXPECTED_HEADER="' + str(expected) + '"', str(probe), str(tinfl), "-lz", "-o", str(executable)]
            subprocess.run(command, check=True)
            run = subprocess.run([str(executable), str(pack)], check=True, text=True, capture_output=True, timeout=60)
            results["esp_wrapper_host_reference"] = {"result": run.stdout.strip(), "not_a_device_rom_test": True,
                                                     "source_sha256": hashlib.sha256(args.tinfl_source.read_bytes()).hexdigest(),
                                                     "source_url": "https://raw.githubusercontent.com/richgel999/miniz/v114/miniz.c"}
    results["pack_sha256"] = manifest["sha256"]
    results["sources"] = {str(p.relative_to(PROJECT)): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in (PROJECT / "main/mimi_assets.c", PROJECT / "main/mimi_assets.h", probe, Path(__file__).resolve())}
    results["temporary_builds_removed"] = True
    (directory / "reader-proof.json").write_text(json.dumps(results, indent=2) + "\n")
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
