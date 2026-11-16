#!/bin/sh
# Capture golden video/audio checksums from the pinned FFmpeg Bink 2 fork.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
MEDIA=${BINK2_TEST_MEDIA:-}
ORACLE_DIR=${BINK2_ORACLE_DIR:-$ROOT/build/oracle}
ORACLE=${FFMPEG_BINK2:-$ORACLE_DIR/FFmpeg-bink2/ffmpeg}
OUT=$ORACLE_DIR/out
PIN=f4904840e820bfd0fb8fbc22179b39dfdbd3a9e0

if [ -z "$MEDIA" ]; then
    echo "BINK2_TEST_MEDIA is not set" >&2
    exit 1
fi

if [ ! -x "$ORACLE" ]; then
    echo "Building pinned FFmpeg Bink 2 oracle in $ORACLE_DIR" >&2
    mkdir -p "$ORACLE_DIR"
    if [ ! -d "$ORACLE_DIR/FFmpeg-bink2" ]; then
        git clone --quiet https://github.com/mrtnmtth/FFmpeg-bink2.git \
            "$ORACLE_DIR/FFmpeg-bink2"
    fi
    (
        cd "$ORACLE_DIR/FFmpeg-bink2"
        git checkout --quiet "$PIN"
        ./configure --disable-doc --enable-gpl --disable-x86asm
        make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
    )
    ORACLE=$ORACLE_DIR/FFmpeg-bink2/ffmpeg
fi

mkdir -p "$OUT" "$ROOT/tests/media"

python3 "$ROOT/tests/make_clip.py" -o "$OUT/clip.yuv"

"$ORACLE" -v error -i "$MEDIA/sc13_01_partial.bk2" -frames:v 236 \
    -f rawvideo -pix_fmt yuv420p - > "$OUT/sc13.yuv"
"$ORACLE" -v error -i "$MEDIA/ge_video_86l.bk2" \
    -f rawvideo -pix_fmt yuv420p - > "$OUT/ge.yuv"

for n in 0 1 2 3 4 5; do
    "$ORACLE" -v error -i "$MEDIA/sc13_01_partial.bk2" -map "0:a:$n" \
        -c:a pcm_s16le -f s16le - > "$OUT/sc13_a$n.s16" 2>>"$OUT/audio.log"
done
"$ORACLE" -v error -i "$MEDIA/ge_video_86l.bk2" -map 0:a:0 \
    -c:a pcm_s16le -f s16le - > "$OUT/ge_a0.s16" 2>>"$OUT/audio.log"

{
    echo '#ifndef LIBBINK2_REFERENCE_CHECKSUMS_H'
    echo '#define LIBBINK2_REFERENCE_CHECKSUMS_H'
    echo
    echo '#include <stdint.h>'
    echo
    echo 'typedef struct {'
    echo '    unsigned long frame;'
    echo '    uint64_t y, u, v, a;'
    echo '} Bink2FrameChecksum;'
    echo
    echo 'typedef struct {'
    echo '    unsigned long total_bytes;'
    echo '    unsigned long prefix_bytes;'
    echo '    uint64_t prefix_hash;'
    echo '} Bink2AudioChecksum;'
    echo
    echo '/* KB2f, 1280x736, complete frames 0..235 of the partial sample. */'
    echo 'static const Bink2FrameChecksum bink2_kb2f_checksums[] = {'
    python3 "$ROOT/tests/checksum_frames.py" --kind video \
        --input "$OUT/sc13.yuv" --width 1280 --height 736 --frames 236
    echo '};'
    echo
    echo '/* KB2g, 1024x576, 490 frames. */'
    echo 'static const Bink2FrameChecksum bink2_kb2g_checksums[] = {'
    python3 "$ROOT/tests/checksum_frames.py" --kind video \
        --input "$OUT/ge.yuv" --width 1024 --height 576 --frames 490
    echo '};'
    echo
    echo '/* KB2f audio: six 44100 Hz mono DCT tracks, first 10 frames pinned. */'
    echo 'static const Bink2AudioChecksum bink2_kb2f_audio[] = {'
    for n in 0 1 2 3 4 5; do
        python3 "$ROOT/tests/checksum_frames.py" --kind audio \
            --input "$OUT/sc13_a$n.s16" --channels 1 --prefix-samples 14714 \
            --prefix-out "$ROOT/tests/media/kb2f_a${n}_10f.s16"
    done
    echo '};'
    echo
    echo '/* KB2g audio: one 48000 Hz stereo DCT track, first 10 frames pinned. */'
    echo 'static const Bink2AudioChecksum bink2_kb2g_audio[] = {'
    python3 "$ROOT/tests/checksum_frames.py" --kind audio \
        --input "$OUT/ge_a0.s16" --channels 2 --prefix-samples 20000 \
        --prefix-out "$ROOT/tests/media/kb2g_a0_10f.s16"
    echo '};'
    echo
    echo '#endif'
} > "$ROOT/tests/reference_checksums.h"

echo "wrote $ROOT/tests/reference_checksums.h"
