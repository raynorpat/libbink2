# Audio encoder design

Status: proposed, awaiting approval.

## Goal

Encode one PCM WAV soundtrack into a `KB2j` file alongside the existing video.
The result must open and play with this decoder, the pinned FFmpeg Bink 2
oracle, and the bundled players.

Audio is lossy, so it is validated with signal-to-noise ratio and a reference
decoder, never by byte comparison.

## Scope

In scope:

- One audio track, mono or 2-channel.
- Bink Audio 2 with the DCT transform (`use_dct=1`, `version_2=1`).
- Signed 16-bit PCM output, as the decoder already produces.
- PCM RIFF WAV input: 8, 16, or 24-bit integer samples, mono or stereo.
- One simple quality control, no bitrate targeting.
- Container track metadata and per-frame audio chunks.

Out of scope for this change:

- Multiple tracks, RDFT blocks, 32-bit float WAV, ADPCM, resampling.
- Audio bitrate targeting, joint stereo, psychoacoustic masking beyond the
  per-band quantiser floor described below.
- A public WAV loader. The library takes PCM through the source callback; the
  WAV loader is an encoder-side helper for the tool.

## Format facts the implementation must match

These come from `src/bink2_audio.c` and the pinned oracle, not from a
specification.

### Stream parameters

`bink2_audio_describe(rate, channels, 1, 1, &info)` derives:

- `frame_len`: 512 below 22050 Hz, 1024 below 44100 Hz, 2048 at and above
  44100 Hz.
- `overlap_len = frame_len / 16`; decoded samples per block
  `block_out = frame_len - overlap_len` (480, 960, or 1920).
- 25 bands or fewer from the WMA critical-frequency table.
- `root = frame_len / (sqrt(frame_len) * 32768)`.
- A 96-entry quantiser table `exp(i * 0.15289164787221953823) * root`.

The encoder reuses `bink2_audio_describe` so the tables cannot drift from the
decoder.

### Chunk and block syntax

A frame's audio chunk is:

```
u32 decoded_pcm_bytes          (skipped by the decoder)
block 0
32-bit alignment
block 1
...
```

Each block:

```
2 bits zero                    (ignored by the decoder)
for each channel:
  float dc0                    (5-bit exponent, 23-bit mantissa, sign)
  float dc1
  7-bit quantiser index per band
  groups of 8 coefficients starting at index 2:
    1 bit  rle
    if rle: 4-bit entry, 4-bit width (zero)
    else:   4-bit width
    if width != 0:
      (j - i) magnitudes of `width` bits
      one sign bit per nonzero magnitude, in order
```

The quantiser index changes when the coefficient index crosses a band
boundary. Width is constant for a group of eight, even when that group spans
two bands. A zero run uses `rle=1`, an entry from
`{2,3,4,5,6,8,9,10,11,12,13,14,15,16,32,64}` multiplied by 8 coefficients,
and a zero width.

`width` is a 4-bit field, so a quantised magnitude must fit in 15 bits
(0..32767). A band's quantiser index must be 0..95.

### Forward transform

The decoder runs `dct_calc_III` and doubles the DC coefficient. The encoder
uses the matching DCT-II:

```
C[k] = Re( exp(-i*pi*k/(2N)) * FFT_2N(x)[k] )
```

with one 2N-point FFT per block, then stores `C[0] * 0.5 / root` and
`C[1] / root` as the two float DC values. This is the same construction libbink
uses for Bink Audio 1 and it inverts the decoder transform; a round-trip test
against `bink2_audio_decode` is the acceptance check, not the derivation.

### Scheduling and overlap

Samples per video frame accumulate as `rate * fps_den / fps_num`. For each
frame, emit whole blocks until the produced sample count reaches the running
target. The source is truncated at the video duration, and a short WAV is
padded with silence. This keeps audio and video duration aligned.

Each block carries `block_out` source samples followed by `overlap_len` samples
of lookahead. The decoder crossfades the block head with the previous block's
tail, and because the previous tail is exactly this block's lookahead, the
crossfade reconstructs the signal instead of smearing it. The final block pads
with silence. The first `overlap_len` samples of the stream fade in from
silence; tests skip that startup transient.

`max_decoded` in the container is an upper bound: the video-duration sample
count plus one `block_out` block, times channels times two. The decoder uses it
as an allocation cap and rejects larger chunks.

## Container

`KB2j` header, one track:

```
offset 0   "KB2j"
offset 4   file size - 8, patched
offset 8   frame count
offset 12  largest packet, patched
offset 16  reserved
offset 20  width
offset 24  height
offset 28  fps numerator
offset 32  fps denominator
offset 36  slice flags
offset 40  audio track count = 1
offset 44  0x87e70800, the KB2i+ field
offset 48  max_decoded
offset 52  rate (24-bit LE) | flags (DCT 0x10, stereo 0x20)
offset 56  track id = 1
offset 60  frame index
```

Each frame is:

```
u32 audio chunk bytes (including the 4-byte decoded size)
audio chunk
video payload
```

A frame with no audio writes a zero chunk length. `largest packet` includes
the audio chunk and its length prefix, so bitrate control on video stays
honest.

Rate is stored as 24 bits. The oracle reads only 16 bits, so the encoder
accepts rates 1..65535 and rejects anything higher.

## Public API

One public-header change, to `src/encoder/bink2_encoder.h`:

```c
typedef struct {
    unsigned long rate;          /* Hz */
    int channels;                /* 1 or 2; 0 disables audio */
    unsigned long sample_count;  /* samples per channel */
} bink2_enc_audio;

typedef struct {
    int width;
    int height;
    unsigned long frame_count;
    unsigned long fps_num;
    unsigned long fps_den;
    bink2_enc_audio audio;
    int (*read_frame)(void *user, unsigned long frame, unsigned char *dst);
    int (*read_frame_yuv)(void *user, unsigned long frame, unsigned char *y,
                          unsigned char *u, unsigned char *v);
    int (*read_audio)(void *user, unsigned long start_sample,
                      unsigned long sample_count, short *dst);
    void *user;
} bink2_enc_source;

typedef struct {
    unsigned keyframe_interval;
    unsigned motion_range;
    unsigned slices;
    int four_mv;
    int quality;
    unsigned target_bitrate;
    int audio_quality;           /* 0..100, default 80 */
    int verify;
    const char *recon_path;
    int verbose;
} bink2_enc_options;
```

`read_audio` fills `sample_count * channels` interleaved host-endian signed
16-bit samples beginning at `start_sample` and returns 0 on success. It must be
repeatable: `verify` reads the source again after encoding. A short final block
is padded with silence by the encoder, so the callback only ever sees indices
below `sample_count`.

`channels == 0` keeps the current video-only behaviour. Any other value
requires `read_audio` and a valid rate.

## CLI

```
bink2_build frames/ -o clip.bk2 --audio soundtrack.wav --audio-quality 80
```

- `--audio FILE` loads a PCM RIFF WAV. Mono or stereo; 8, 16, or 24-bit.
- `--audio-quality N` is 0..100, default 80. It applies only when `--audio` is
  present.
- The WAV is truncated or padded to the video duration. No resampling: the
  container stores the WAV rate.
- Reject float/ADPCM WAV, more than two channels, zero rate, and rates above
  65535.

The WAV loader follows `bink2_enc_image.c`: a small `bink2_enc_wav.c/.h` in the
encoder library, with no dependency and no public header.

## Quality control

For each band, the encoder picks the smallest quantiser index whose step keeps
the band's loudest coefficient below a target number of steps:

```
target = 8 << (quality * 10 / 100)       /* 8 .. 8192, clamp to 32767 */
q = smallest index with peak / quant[i] <= target
```

A floor borrowed from libbink prevents quiet high-frequency bands from
spending full precision on spectral leakage: no band may use an index more
than 12 entries below the loudest band's index. Widths are then computed from
the actual quantised magnitudes, so a band can use fewer bits than its target
implies.

If the loudest coefficient still needs more than 15 bits at index 95, the
encoder clamps the magnitude to 32767. This only affects extreme full-scale
high-frequency content and is logged in `--verbose`.

The first implementation will record measured SNR and size at several quality
values in the test comments; the tests then pin a floor rather than a guessed
number.

## Modules

| File | Change |
| --- | --- |
| `src/encoder/bink2_enc_audio.c/.h` | New: scheduler, forward DCT, block writer, chunk assembly. |
| `src/encoder/bink2_enc_wav.c/.h` | New: PCM WAV loader to interleaved s16. |
| `src/encoder/bink2_enc_container.c/.h` | Header track metadata, zero-chunk support. |
| `src/encoder/bink2_encoder.c` | Validate audio source, open/close audio state, write chunks before video, extend verify. |
| `src/encoder/bink2_encoder.h` | The public API above. |
| `src/encoder/bink2_enc_internal.h` | Declare the audio source helper if needed. |
| `src/tools/bink2_build.c` | `--audio`, `--audio-quality`, WAV loading, source callback. |
| `CMakeLists.txt` | Add both encoder sources. |
| `tests/CMakeLists.txt` | New test targets. |

Verification: `bink2_enc_encode_intra` currently decodes the file and compares
video frames. With audio it additionally enables track 0, requires every frame
to decode, and requires the accumulated PCM to cover the video duration. It
does not require audio to match the source, because the transform is lossy.

## Tests

1. `test_enc_wav` (unit): 8/16/24-bit mono and stereo, odd chunk padding,
   unknown chunks, truncated data, non-PCM tags, and unsupported channel
   counts. No sample files.
2. `test_enc_dct` (existing file or a new unit): forward DCT followed by the
   decoder's transform reproduces the input block within float tolerance;
   covers frame lengths 512, 1024, and 2048.
3. `test_enc_audio`: for each rate class, encode a synthetic block, decode it
   with `bink2_audio_decode`, and check the reported decoded size, sample
   count, and SNR after skipping the startup overlap. Includes an all-zero
   block, a DC-only block, and a full-scale sine.
4. `test_enc_syntax` addition: write one multi-block chunk, parse it with the
   decoder's bit reader using the documented grammar, and assert the reader
   ends exactly at the chunk's byte length. This is the writer/reader
   bit-position rule.
5. `test_enc_api`: a video plus audio source encodes, reopens with
   `bink2_info_audio`, reports one track with the WAV rate and channel count,
   decodes every frame, and returns non-silent PCM. Also covers a source with
   `channels == 0` unchanged, and invalid audio metadata rejected as usage.
6. `test_enc_rate` addition: default quality, lower quality, and all-silence
   produce non-increasing sizes, and quality 0 still decodes.
7. `test_malformed` additions: an audio chunk larger than the frame, a
   truncated decoded-size prefix, a track descriptor with a zero rate, and a
   chunk whose reported size exceeds `max_decoded`. Run under ASan/UBSan.
8. `build_e2e`: `bink2_build --audio` on generated frames and a generated WAV,
   then open the result and verify track metadata and decoded audio.
9. `test_enc_audio_oracle` (optional, skips with 77 when `BINK2_ORACLE` is
   unset): encode a short clip with audio, decode it with this library, decode
   it with the pinned FFmpeg Bink 2 build, and compare the two PCM streams.
   The two transforms round some samples differently, so the test allows at
   most a one-LSB difference and reports the SNR against the source. This does
   not modify `reference_checksums.h`.

No reference checksum is regenerated, and no binary audio fixture is added.

## Risks and limits

- **Quantiser saturation.** The 15-bit width and 95 quantiser indices bound the
  representable coefficient magnitude. Full-scale high-frequency content can
  clip. The test suite includes a full-scale sine to make this visible.
- **Quality tuning.** The target-steps formula and the 12-entry mask floor are
  starting points. They will be adjusted against measured SNR and bitrate, and
  the final numbers recorded in the tests.
- **Startup transient.** The first block fades in from silence. This matches
  the format's overlap behaviour; seeking already resets overlap.
- **16-bit rate ceiling.** The oracle's demuxer reads a 16-bit rate. The
  encoder rejects higher rates instead of writing a file the oracle
  misparses.
- **Lossy output.** There is no bit-exact audio guarantee, and none is
  possible. Video reconstruction remains bit-exact.

## Implementation order

1. WAV loader and its unit test.
2. Forward DCT and the round-trip test.
3. Block writer, syntax test, and decoder round-trip.
4. Scheduler, chunk assembly, and duration test.
5. Container metadata, public API, encode loop, and verify.
6. `bink2_build --audio`, help text, and end-to-end test.
7. Malformed cases, oracle comparison, docs, full release and sanitizer runs.

## Approval

Please confirm this design, especially:

- the public API shape (`bink2_enc_audio` inside `bink2_enc_source`,
  `audio_quality` in the options), and
- the 1..65535 Hz limit with no resampling.

With approval I will write the implementation plan and then build it in the
order above.
