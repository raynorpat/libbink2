# Audio encoder design

How the encoder writes its Bink Audio 2 track: the format facts it must match,
the container layout, quality control, and the limits. For the API fields see
[api.md](api.md#encoder-bink2_encoderh); for the `--audio` flags see
[tools.md](tools.md#bink2_build).

## Goal

Encode one PCM soundtrack into a `KB2j` file alongside the video. The result
must open and play with this decoder, the pinned FFmpeg Bink 2 oracle, and the
bundled players.

Audio is lossy, so it is validated with signal-to-noise ratio and a reference
decoder, never by byte comparison.

## Scope

Supported:

- One audio track, mono or 2-channel.
- Bink Audio 2 with the DCT transform (`use_dct=1`, `version_2=1`).
- Signed 16-bit PCM output, as the decoder already produces.
- PCM RIFF WAV input: 8, 16, or 24-bit integer samples, mono or stereo.
- One simple quality control, no bitrate targeting.
- Container track metadata and per-frame audio chunks.

Not supported:

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

Audio is configured through `source.audio` (`rate`, `channels`,
`sample_count`), the `read_audio` callback, and `options.audio_quality`; see
[api.md](api.md#source). `read_audio` must be repeatable because `verify`
reads the source again after encoding. The encoder pads past the end of the
source itself, so the callback only sees indices below `sample_count`.

`channels == 0` keeps video-only behaviour. Any other value requires
`read_audio` and a valid rate, or the encode fails with
`BINK2_ENC_ERR_USAGE`.

## CLI

`bink2_build --audio FILE --audio-quality N` loads a PCM RIFF WAV with
`bink2_enc_wav.c` and passes it through `read_audio`. The loader is an
encoder-side helper with no public header. Float and ADPCM WAV, more than two
channels, a zero rate, and rates above 65535 are rejected. See
[tools.md](tools.md#bink2_build).

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
high-frequency content.

`test_enc_audio` pins SNR floors and size ordering across quality values
rather than exact sizes.

## Modules

| File | Responsibility |
| --- | --- |
| `src/encoder/bink2_enc_audio.c/.h` | Scheduler, forward DCT, block writer, chunk assembly. |
| `src/encoder/bink2_enc_wav.c/.h` | PCM WAV loader to interleaved s16. |
| `src/encoder/bink2_enc_container.c/.h` | Track metadata in the header, zero-length chunks. |
| `src/encoder/bink2_encoder.c` | Audio source validation, chunks written before video, audio checks in `verify`. |
| `src/tools/bink2_build.c` | `--audio`, `--audio-quality`, WAV loading, the source callback. |

With audio, `verify` enables track 0, requires every frame to decode, and
requires the decoded PCM byte count to equal the encoder's total. It does not
compare samples with the source, because the transform is lossy.

## Tests

No sample files are needed, no reference checksum depends on the audio
encoder, and no binary audio fixture is committed.

| Test | Covers |
| --- | --- |
| `enc_wav` | 8/16/24-bit mono and stereo WAV, odd chunk padding, unknown chunks, truncated data, non-PCM tags, unsupported channel counts. |
| `enc_audio` | Decoder round trip and SNR per rate class; silence, DC, full-scale high-frequency content; quality ordering; scheduling and video-duration truncation; writer and reader ending at the same bit position. |
| `enc_container_audio` | Header track descriptor, stereo and mono flags, the no-track case. |
| `enc_audio_file` | Whole-file encodes with audio, short-source padding, packet sizes, a corrupted reported-size field. |
| `enc_api` | Audio field and `audio_quality` validation, and a failing `read_audio` callback. |
| `malformed` | Oversized and truncated audio chunks, a reported size above `max_decoded`, a zero-rate track descriptor. |
| `build_e2e` | `bink2_build --audio` end to end. |
| `enc_audio_oracle` | Library and pinned-oracle decodes agree within one LSB. Skips (77) without `BINK2_ORACLE`; see [tests/README.md](../tests/README.md#encoded-audio-oracle). |

## Risks and limits

- **Quantiser saturation.** The 15-bit width and 95 quantiser indices bound the
  representable coefficient magnitude. Full-scale high-frequency content can
  clip. The test suite includes a full-scale sine to make this visible.
- **Quality tuning.** The target-steps formula and the 12-entry mask floor are
  heuristics, not a psychoacoustic model. Changing them changes every encoded
  file; the SNR floors in `test_enc_audio` are the check.
- **Startup transient.** The first block fades in from silence. This matches
  the format's overlap behaviour; seeking already resets overlap.
- **16-bit rate ceiling.** The oracle's demuxer reads a 16-bit rate. The
  encoder rejects higher rates instead of writing a file the oracle
  misparses.
- **Lossy output.** There is no bit-exact audio guarantee, and none is
  possible. Video reconstruction remains bit-exact.
