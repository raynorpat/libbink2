#!/usr/bin/env python3
"""Write a deterministic YUV 4:2:0 clip for encoder and oracle tests."""

import argparse


def clip(width, height, frames):
    out = bytearray()
    cw = (width + 1) // 2
    ch = (height + 1) // 2
    for f in range(frames):
        for y in range(height):
            for x in range(width):
                out.append((x + y + f * 3) & 0xFF)
        for y in range(ch):
            for x in range(cw):
                out.append((128 + x + f) & 0xFF)
        for y in range(ch):
            for x in range(cw):
                out.append((128 - y + f) & 0xFF)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--width", type=int, default=320)
    ap.add_argument("--height", type=int, default=240)
    ap.add_argument("--frames", type=int, default=64)
    args = ap.parse_args()
    data = clip(args.width, args.height, args.frames)
    with open(args.output, "wb") as f:
        f.write(data)
    print(f"{args.output}: {args.width}x{args.height} {args.frames} frames, {len(data)} bytes")


if __name__ == "__main__":
    main()
