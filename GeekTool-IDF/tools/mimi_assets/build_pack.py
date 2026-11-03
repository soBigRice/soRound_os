#!/usr/bin/env python3
"""生成完整、无损的小咪素材，原始中间文件逐帧清理。 / Build lossless Mimi assets, cleaning raw files after each frame."""
from pathlib import Path
import argparse
import hashlib
import json
import shlex
import shutil
import struct
import subprocess
import zlib

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
STREAMS = ("base_rows", "span_rows", "body_rle", "base_rle", "spans")
HEADER = struct.Struct("<8sIIIHHHHIIII16sI")
INDEX = struct.Struct("<IHBB5I5II12s")
FRAME_COUNT = 186


def sha(data):
    return hashlib.sha256(data).hexdigest()


def rle(data, unit):
    assert len(data) % unit == 0
    values = [data[i:i + unit] for i in range(0, len(data), unit)]
    out = bytearray()
    at = 0
    while at < len(values):
        end = at + 1
        while end < len(values) and end - at < 32767 and values[end] == values[at]:
            end += 1
        if end - at >= 3:
            out += struct.pack("<H", 0x8000 | (end - at)) + values[at]
            at = end
            continue
        begin = at
        at = end
        while at < len(values) and at - begin < 32767:
            end = at + 1
            while end < len(values) and end - at < 3 and values[end] == values[at]:
                end += 1
            if end - at >= 3:
                break
            at = min(end, begin + 32767)
        out += struct.pack("<H", at - begin) + b"".join(values[begin:at])
    return bytes(out)


def unrle(data, unit):
    at = 0
    out = bytearray()
    while at < len(data):
        token = struct.unpack_from("<H", data, at)[0]
        at += 2
        n = token & 32767
        assert n
        if token & 32768:
            out += data[at:at + unit] * n
            at += unit
        else:
            out += data[at:at + n * unit]
            at += n * unit
    assert at == len(data)
    return bytes(out)


def deflate(raw):
    compressor = zlib.compressobj(9, zlib.DEFLATED, -15)
    packed = compressor.compress(raw) + compressor.flush()
    assert zlib.decompress(packed, -15) == raw
    return packed


def header(file_bytes, indices):
    raw = HEADER.pack(b"MIMIPAK\0", 1, 64, FRAME_COUNT, 466, 466, 50, 5,
                      len(indices), file_bytes, zlib.crc32(indices), 1, bytes(16), 0)
    return raw[:60] + struct.pack("<I", zlib.crc32(raw[:60]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host-build", type=Path, default=PROJECT / "build/mimi-full-host")
    parser.add_argument("--out", type=Path, default=PROJECT / "build/flash-records/mimi-full-20261103/assets")
    # canonical仅通过主机像素检查，真机未启用加速；默认必须使用已验证表示。
    # Canonical passed host pixels but failed target-path coverage; default to the verified representation.
    parser.add_argument("--original-spans", action=argparse.BooleanOptionalAction, default=True,
                        help="Keep verified original streams (default); --no-original-spans reproduces the failed canonical experiment only")
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    if (out / "MIMI.PAK").exists():
        raise RuntimeError("A frozen pack already exists; select a new --out directory")
    rawdir = out / "raw-frame"
    rawdir.mkdir(exist_ok=True)
    if list(rawdir.iterdir()):
        raise RuntimeError("Unreviewed raw files exist; do not overwrite them")
    host = args.host_build.resolve() / "mimi"
    link = shlex.split((host / "CMakeFiles/mimi_ui_tests.dir/link.txt").read_text())
    executable, obj, canonical_obj = out / "native-generate", out / "native-generate.o", out / "front-canonical.o"
    flags = [link[0], "-O3", "-g", '-DLV_CONF_PATH="' + str(PROJECT / "tests/host/lv_conf.h") + '"',
             "-DLV_KCONFIG_IGNORE", "-DLV_LVGL_H_INCLUDE_SIMPLE"]
    for include in ("tests/host/stubs", "main", "managed_components/lvgl__lvgl", "tests/host"):
        flags.append("-I" + str(PROJECT / include))
    sources = [HERE / "native_generate.c", HERE / "native_ui.h", HERE / "front_canonical.c", HERE / "front_canonical.h", PROJECT / "main/app_mimi.c",
               PROJECT / "main/mimi_pet.c", PROJECT / "main/mimi_motion.c", PROJECT / "main/mimi_motions.inc"]
    source_hashes = {str(p.relative_to(PROJECT)): sha(p.read_bytes()) for p in sources}
    try:
        subprocess.run(flags + ["-c", str(HERE / "native_generate.c"), "-o", str(obj)], check=True)
        subprocess.run(flags + ["-c", str(HERE / "front_canonical.c"), "-o", str(canonical_obj)], check=True)
        link = [str(obj) if x == "CMakeFiles/mimi_ui_tests.dir/ui_tests.c.o" else x for x in link]
        link.insert(link.index("-o"), str(canonical_obj))
        link[link.index("-o") + 1] = str(executable)
        subprocess.run(link, cwd=host, check=True)
    except BaseException:
        obj.unlink(missing_ok=True)
        canonical_obj.unlink(missing_ok=True)
        executable.unlink(missing_ok=True)
        if not list(rawdir.iterdir()):
            rawdir.rmdir()
        raise
    child = None
    stderr = None
    temporary = out / "MIMI.PAK.tmp"
    frames, indices, deltas = [], bytearray(), []
    payload_bytes = 0
    try:
        stderr = (out / "native-stderr.log").open("w")
        child = subprocess.Popen([str(executable), str(rawdir)], stdin=subprocess.PIPE,
                                 stdout=subprocess.PIPE, stderr=stderr, text=True)
        with temporary.open("wb+") as pack, (out / "native-proof.log").open("w") as log:
            pack.write(bytes(64 + FRAME_COUNT * 64))
            for line in child.stdout:
                fields = dict(item.split("=", 1) for item in line.split())
                assert fields["exact"] == "true" and fields["canonical_exact"] == "true"
                i, clip, ms = int(fields["frame"]), int(fields["clip"]), int(fields["ms"])
                assert i == len(frames)

                def read(kind):
                    return (rawdir / f"{kind}-{i:02}.bin").read_bytes()

                base, body, spans, rows = read("base"), read("body"), read("spans"), read("rows")
                reference, masks, eyes = read("reference"), read("masks"), read("eyes")
                assert len(base) == 466 * 466 * 2 and len(body) == 466 * 466
                assert len(spans) == int(fields["spans"]) * 8
                row_offsets = struct.unpack("<467I", rows)
                assert row_offsets[0] == 0 and row_offsets[-1] == len(spans) // 8
                assert all(a <= b for a, b in zip(row_offsets, row_offsets[1:]))
                assert max(b - a for a, b in zip(row_offsets, row_offsets[1:])) <= 623
                for x, n, _, q, pad in struct.iter_unpack("<HHHBB", spans):
                    assert n and x + n <= 466 and 1 <= q <= 32 and pad == 0
                chunks = [rle(base[y * 932:(y + 1) * 932], 2) for y in range(466)]
                assert all(unrle(chunk, 2) == base[y * 932:(y + 1) * 932] for y, chunk in enumerate(chunks))
                assert max(map(len, chunks)) <= 936
                offsets = [0]
                for chunk in chunks:
                    offsets.append(offsets[-1] + len(chunk))
                br, mr = b"".join(chunks), rle(body, 1)
                assert unrle(mr, 1) == body
                raw = (struct.pack("<467I", *offsets), rows, mr, br, spans)
                assert sum((len(stream) + 3) & ~3 for stream in raw) <= 416800
                packed = tuple(deflate(stream) for stream in raw)
                assert sum(map(len, packed)) <= 96035
                original_raw, original_packed = raw, packed
                candidate_spans, candidate_rows = read("canonical-spans"), read("canonical-rows")
                assert len(candidate_spans) == int(fields["canonical_spans"]) * 8 and len(candidate_rows) == 467 * 4
                candidate_offsets = struct.unpack("<467I", candidate_rows)
                assert candidate_offsets[0] == 0 and candidate_offsets[-1] == len(candidate_spans) // 8
                assert all(a <= b for a, b in zip(candidate_offsets, candidate_offsets[1:]))
                max_row = max(b - a for a, b in zip(candidate_offsets, candidate_offsets[1:]))
                assert max_row == int(fields["canonical_max_row"])
                for x, n, _, q, pad in struct.iter_unpack("<HHHBB", candidate_spans):
                    assert n and x + n <= 466 and 1 <= q <= 32 and pad == 0
                candidate_raw = (raw[0], candidate_rows, raw[2], raw[3], candidate_spans)
                candidate_packed = (packed[0], deflate(candidate_rows), packed[2], packed[3], deflate(candidate_spans))
                has_q32 = int(fields["canonical_opaque_pixels"]) > 0
                aligned_raw = sum((len(stream) + 3) & ~3 for stream in candidate_raw)
                # 静态表示选择只允许raw不增、压缩减小且符合既有reader容量；否则原流与顺序原样保留。
                # Choose the static representation only when raw does not grow, compression improves and existing reader bounds hold; otherwise keep original bytes/order.
                selected = bool(not args.original_spans and has_q32 and len(candidate_spans) <= len(spans)
                                and sum(map(len, candidate_packed)) < sum(map(len, packed))
                                and max_row <= 623 and aligned_raw <= 416800 and sum(map(len, candidate_packed)) <= 96035)
                if selected:
                    raw, packed = candidate_raw, candidate_packed
                else:
                    assert raw == original_raw and packed == original_packed
                assert all(raw[stream] == original_raw[stream] and packed[stream] == original_packed[stream] for stream in (0, 2, 3))
                transparent = int(fields["canonical_transparent_pixels"])
                assert int(fields["canonical_unchanged_sequences"]) == transparent
                assert int(fields["canonical_background_checks"]) == 8 * (int(fields["canonical_opaque_pixels"]) + transparent)
                assert int(fields["canonical_exhaustive_checks"]) == (65536 if has_q32 else 0)
                deltas.append({"frame": i, "clip": clip, "ms": ms, "selected": selected, "has_q32": has_q32,
                               "old_raw_span_bytes": len(spans), "candidate_raw_span_bytes": len(candidate_spans), "raw_span_bytes": len(raw[4]),
                               "old_packed_sizes": list(map(len, original_packed)), "candidate_packed_sizes": list(map(len, candidate_packed)), "packed_sizes": list(map(len, packed)),
                               "candidate_max_row": max_row, "max_row": max(b - a for a, b in zip(candidate_offsets if selected else row_offsets, (candidate_offsets if selected else row_offsets)[1:])),
                               "candidate_aligned_raw_frame_bytes": aligned_raw, "aligned_raw_frame_bytes": sum((len(stream) + 3) & ~3 for stream in raw),
                               "old_pixel_ops": int(fields["old_pixel_ops"]), "pixel_ops": int(fields["canonical_pixel_ops"]) if selected else int(fields["old_pixel_ops"]),
                               "opaque_pixels": int(fields["canonical_opaque_pixels"]), "unchanged_no_q32_sequences": transparent,
                               "background_checks": int(fields["canonical_background_checks"]), "exhaustive_background_checks": int(fields["canonical_exhaustive_checks"]),
                               "longest_opaque_sample_depth": int(fields["canonical_sample_depth"]), "candidate_default_native_exact": True,
                               "other_three_streams_unchanged": True, "unselected_front_bytes_unchanged": not selected})
                crc = 0
                for stream in raw:
                    crc = zlib.crc32(stream, crc)
                position = pack.tell()
                indices += INDEX.pack(position, ms, clip, 0, *map(len, raw), *map(len, packed), crc, bytes(12))
                for stream in packed:
                    pack.write(stream)
                payload_bytes += sum(map(len, packed))
                pack.write(bytes((-pack.tell()) % 4))
                swapped = bytearray(len(reference))
                swapped[0::2], swapped[1::2] = reference[1::2], reference[0::2]
                frames.append({"frame": i, "clip": clip, "ms": ms, "offset": position,
                               "raw_sizes": list(map(len, raw)), "packed_sizes": list(map(len, packed)),
                               "unpacked_crc32": crc,
                               "raw_sha256": dict(zip(STREAMS, map(sha, raw))),
                               "original_front_raw_sha256": {"span_rows": sha(rows), "spans": sha(spans)}, "front_canonical_selected": selected,
                               "reference_sha256": sha(reference), "reference_crc32": zlib.crc32(reference),
                               "swapped_sha256": sha(swapped), "swapped_crc32": zlib.crc32(swapped),
                               "body_sha256": sha(body), "body_crc32": zlib.crc32(body),
                               "masks_sha256": sha(masks),
                               "eye_alpha_sha256": [sha(masks[466 * 466 + e * 208 * 208:466 * 466 + (e + 1) * 208 * 208]) for e in range(2)],
                               "eye_metadata": [list(x) for x in zip(*[iter(struct.unpack("<14i", eyes))] * 7)],
                               "native_exact": True})
                log.write(line)
                log.flush()
                # C进程收到确认才写下一帧，避免累积几百MB原始图片。
                # The C producer waits for acknowledgement, so raw images never accumulate across frames.
                for file in rawdir.iterdir():
                    file.unlink()
                child.stdin.write("\n")
                child.stdin.flush()
            code = child.wait()
            if code:
                raise RuntimeError(f"Native generator failed ({code}); see native-stderr.log")
            assert len(frames) == FRAME_COUNT and len(indices) == FRAME_COUNT * 64
            assert [(f["clip"], f["ms"]) for f in frames] == [(clip, ms) for clip, count in ((10, 50), (11, 78), (13, 58)) for ms in range(0, count * 50, 50)]
            length = pack.tell()
            pack.seek(0)
            pack.write(header(length, indices))
            pack.write(indices)
        temporary.replace(out / "MIMI.PAK")
        optimization = {"revision": 1, "enabled": not args.original_spans, "selected_frames": sum(f["selected"] for f in deltas),
                        "unchanged_frames": sum(not f["selected"] for f in deltas),
                        "old_raw_span_bytes": sum(f["old_raw_span_bytes"] for f in deltas), "raw_span_bytes": sum(f["raw_span_bytes"] for f in deltas),
                        "old_pixel_ops": sum(f["old_pixel_ops"] for f in deltas), "pixel_ops": sum(f["pixel_ops"] for f in deltas),
                        "background_checks": sum(f["background_checks"] for f in deltas), "exhaustive_background_checks": sum(f["exhaustive_background_checks"] for f in deltas),
                        "no_q32_sequences_preserved": sum(f["unchanged_no_q32_sequences"] for f in deltas),
                        "max_row_spans": max(f["max_row"] for f in deltas), "max_aligned_raw_frame_bytes": max(f["aligned_raw_frame_bytes"] for f in deltas),
                        "max_packed_frame_bytes": max(sum(f["packed_sizes"]) for f in deltas),
                        "old_payload_bytes": sum(sum(f["old_packed_sizes"]) for f in deltas), "payload_bytes": payload_bytes,
                        "old_pack_bytes": 64 + 64 * FRAME_COUNT + sum((sum(f["old_packed_sizes"]) + 3) & ~3 for f in deltas), "pack_bytes": length,
                        "pointwise_proof": "mix565(c,b,32)=c for every RGB565 background; discard only the prefix before the final q32, evaluate its suffix with the original per-operation rounding, and replace that pixel stack by the final constant q32. Without q32 the pixel sequence is unchanged; regrouping different pixels by depth preserves each pixel's sequence.",
                        "no_runtime_or_format_change": True}
        (out / "canonical-delta.json").write_text(json.dumps({"summary": optimization, "frames": deltas}, indent=2) + "\n")
        result = {"version": 1, "frame_count": FRAME_COUNT, "all_native_exact": True, "foreground_optimization": optimization,
                  "stream_order": STREAMS, "size": length, "payload_bytes": payload_bytes,
                  "sha256": sha((out / "MIMI.PAK").read_bytes()), "source_hashes_at_compile": source_hashes,
                  "source_hashes": {str(p.relative_to(PROJECT)): sha(p.read_bytes()) for p in (HERE / "build_pack.py", HERE / "native_generate.c", HERE / "native_ui.h", HERE / "front_canonical.c", HERE / "front_canonical.h")},
                  "raw_intermediates_removed": True, "frames": frames}
        (out / "manifest.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps({k: v for k, v in result.items() if k not in ("frames", "source_hashes_at_compile", "source_hashes")}))
    finally:
        if child is not None:
            if child.poll() is None:
                child.terminate()
                child.wait()
            child.stdin.close()
            child.stdout.close()
        if stderr is not None:
            stderr.close()
        for path in (executable, obj, canonical_obj):
            path.unlink(missing_ok=True)
        shutil.rmtree(executable.with_suffix(".dSYM"), ignore_errors=True)
        if rawdir.exists() and not list(rawdir.iterdir()):
            rawdir.rmdir()
        temporary.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
