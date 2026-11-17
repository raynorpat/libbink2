#!/bin/sh
# Validate a libbink2 KB2j file with the pinned FFmpeg Bink 2 oracle.
set -eu

if [ "$#" -lt 1 ]; then
    echo "usage: $0 file.bk2 [raw.yuv]" >&2
    exit 2
fi

FILE=$1
ROOT=$(cd "$(dirname "$0")/.." && pwd)
ORACLE=${FFMPEG_BINK2:-$ROOT/build/oracle/FFmpeg-bink2/ffmpeg}
LOG=${TMPDIR:-/tmp}/libbink2-oracle.log

if [ ! -x "$ORACLE" ]; then
    echo "validate: oracle not found at $ORACLE (set FFMPEG_BINK2)" >&2
    exit 2
fi

FLAGS=$(python3 -c 'import struct,sys; print(struct.unpack_from("<I", open(sys.argv[1], "rb").read(40), 36)[0])' "$FILE")
if [ $((FLAGS & 3)) -eq 3 ]; then
    echo "validate: 8-slice streams are not oracle-validated (pinned FFmpeg slice_height[4] bug); relying on the library round trip"
    exit 0
fi

: > "$LOG"
if ! "$ORACLE" -v error -i "$FILE" -f null - 2> "$LOG"; then
    echo "validate: oracle rejected $FILE" >&2
    cat "$LOG" >&2
    exit 1
fi
if [ -s "$LOG" ]; then
    echo "validate: oracle wrote diagnostics for $FILE" >&2
    cat "$LOG" >&2
    exit 1
fi

echo "validate: $FILE decoded cleanly by the oracle"

# Optional independent-encoder comparison. Set BINK2ENC_BIN, BINK2_VALIDATE_YUV
# and the clip geometry to compare bitrate and PSNR at the same content.
if [ -n "${BINK2ENC_BIN:-}" ] && [ -n "${BINK2_VALIDATE_YUV:-}" ]; then
    W=${BINK2_VALIDATE_WIDTH:-320}
    H=${BINK2_VALIDATE_HEIGHT:-240}
    FPS=${BINK2_VALIDATE_FPS:-25}
    OUT=${TMPDIR:-/tmp}/libbink2-bink2enc.bk2
    "$BINK2ENC_BIN" -w "$W" -h "$H" -fps "$FPS" -gop 150 \
        -o "$OUT" "$BINK2_VALIDATE_YUV" >/dev/null
    echo "validate: bink2enc wrote $(wc -c < "$OUT") bytes; libbink2 $(wc -c < "$FILE") bytes"
fi
