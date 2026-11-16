#!/usr/bin/env python3
"""Generate C checksum initialisers for the pinned Bink 2 reference output."""

import argparse

FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211


def fnv(data):
    h = FNV_OFFSET
    for b in data:
        h ^= b
        h = (h * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return h


def video_entries(path, width, height, frames, pix_fmt):
    data = open(path, "rb").read()
    cw = (width + 1) // 2
    ch = (height + 1) // 2
    y_size = width * height
    c_size = cw * ch
    a_size = width * height if pix_fmt == "yuva420p" else 0
    frame_size = y_size + 2 * c_size + a_size
    if len(data) < frame_size * frames:
        raise SystemExit(f"{path}: need {frame_size * frames} bytes, have {len(data)}")
    for f in range(frames):
        off = f * frame_size
        y = fnv(data[off:off + y_size]); off += y_size
        u = fnv(data[off:off + c_size]); off += c_size
        v = fnv(data[off:off + c_size]); off += c_size
        a = 0
        if a_size:
            a = fnv(data[off:off + a_size]); off += a_size
        print(f"    {{ {f}, 0x{y:016x}ULL, 0x{u:016x}ULL, 0x{v:016x}ULL, 0x{a:016x}ULL }},")


def audio_entry(path, channels, prefix_samples, prefix_out):
    data = open(path, "rb").read()
    frame_bytes = 2 * channels
    prefix_bytes = min(len(data), prefix_samples * frame_bytes)
    prefix = data[:prefix_bytes]
    if prefix_out:
        with open(prefix_out, "wb") as f:
            f.write(prefix)
    print(f"    {{ {len(data)}, {len(prefix)}, 0x{fnv(prefix):016x}ULL }},")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kind", choices=["video", "audio"], required=True)
    ap.add_argument("--input", required=True)
    ap.add_argument("--width", type=int, default=0)
    ap.add_argument("--height", type=int, default=0)
    ap.add_argument("--frames", type=int, default=0)
    ap.add_argument("--pix-fmt", default="yuv420p")
    ap.add_argument("--channels", type=int, default=2)
    ap.add_argument("--prefix-samples", type=int, default=0)
    ap.add_argument("--prefix-out", default="")
    args = ap.parse_args()
    if args.kind == "video":
        video_entries(args.input, args.width, args.height, args.frames,
                      args.pix_fmt)
    else:
        audio_entry(args.input, args.channels, args.prefix_samples,
                    args.prefix_out)


if __name__ == "__main__":
    main()
