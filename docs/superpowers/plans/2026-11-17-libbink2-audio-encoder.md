# Audio Encoder Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Encode one mono or stereo PCM WAV track into a `KB2j` file using Bink Audio 2 DCT blocks, with a simple quality control.

**Architecture:** A new encoder-side audio module derives its frame length, bands, quantiser table, and root scale from the decoder's `bink2_audio_describe`, applies a forward DCT-II, quantises per band, and writes the decoder's block syntax. The encoder loop emits a four-byte chunk length, the audio chunk, then the existing video payload; the container header gains one track descriptor. A WAV loader feeds PCM to the encoder through a new source callback.

**Tech Stack:** C99, libc/libm, the existing `bink2_bits` writer/reader, and the existing encoder test harness. No new dependency.

**Spec:** `docs/audio-encoder-design.md`

## Global Constraints

- C99 with `C_EXTENSIONS OFF`, warning-free under `-Wall -Wextra`.
- Four-space indent, no tabs, about 80 columns, function braces on their own line.
- Public names `bink2_enc_*`; internal types `Bink2Enc*`; file-local functions `static`.
- Every new source goes into `BINK2_ENCODER_SOURCES`; every new test needs an `add_executable` and `add_test` pair.
- Public-header changes are limited to the `bink2_enc_audio` struct, the two new `bink2_enc_source` fields, and `audio_quality`. Nothing else in `bink2.h` or `bink2_encoder.h` changes.
- Supported audio: 1 or 2 channels, rate 1..65535 Hz, signed 16-bit PCM. No resampling. No multi-track, no RDFT.
- Do not edit `tests/reference_checksums.h`, `tests/media/`, or `docs/superpowers/`.
- A test that cannot run returns 77, never 0.
- Audio is lossy; never claim byte-exact audio. Video reconstruction stays byte-exact.
- Run the release and ASan/UBSan suites before reporting completion, and report skips as skips.
- Conventional Commit subjects, one logical change per commit.

## Review Focus

1. **A 0 dBFS high-frequency tone** must encode without failing and must not silently wrap a coefficient past the 15-bit width. A reasonable user expects loud treble to get coarser, not to distort into a different waveform.
2. **A WAV with odd-sized chunks before `data`** (including a `JUNK` chunk with odd length) must load the correct samples and not loop or overread the file.
3. **A WAV longer or shorter than the video** must be truncated or padded so the decoded PCM byte count equals the sum of the per-frame `pcm_bytes`; playback should not drift or end mid-block.
4. **A corrupt audio chunk length or decoded-size field** must not read past the frame: an impossible chunk length reports an error with no PCM, while a bad decoded-size in an otherwise intact frame leaves the video decodable and the audio track empty.
5. **Rates at the 22050 and 44100 boundaries** change `frame_len`, band count, and root scale. All three rate classes must encode and decode with the same tests, not just 48000.

---

### Task 1: PCM WAV loader

**Files:**
- Create: `src/encoder/bink2_enc_wav.h`
- Create: `src/encoder/bink2_enc_wav.c`
- Create: `tests/test_enc_wav.c`
- Modify: `CMakeLists.txt` (`BINK2_ENCODER_SOURCES`)
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:

```c
typedef struct {
    unsigned long rate;          /* Hz, 1..65535 */
    int channels;                /* 1 or 2 */
    unsigned long sample_count;  /* per channel */
    short *pcm;                  /* interleaved host-endian s16 */
} Bink2EncWav;

int bink2_enc_wav_load(const char *path, Bink2EncWav *wav, char *err,
                       size_t errlen);
void bink2_enc_wav_free(Bink2EncWav *wav);
```

`bink2_enc_wav_load` returns 0 or -1; on failure it writes a short NUL-terminated message into `err` when `err != NULL && errlen > 0`, frees any partial allocation, and leaves `wav` zeroed. `bink2_enc_wav_free` tolerates NULL and repeated calls.

- [ ] **Step 1: Write the failing test**

Create `tests/test_enc_wav.c` with a `write_wav(path, bits, channels, rate, frames, payload, payload_size)` helper that writes `RIFF/WAVE`, a `fmt ` chunk (tag 1, block align, byte rate, 16-byte PCM), and a `data` chunk. Add cases:

```c
CHECK(test_16_bit_mono() == 0);      /* exact -32768, -1, 0, 1, 32767 */
CHECK(test_8_bit_stereo() == 0);     /* 0x00 -> -32768, 0x80 -> 0, 0xFF -> 32512 */
CHECK(test_24_bit_stereo() == 0);    /* round-to-nearest, clamp at the edges */
CHECK(test_odd_chunks() == 0);       /* JUNK length 3 followed by fmt and data */
CHECK(test_truncated_data() == 0);   /* data size claims 64, file has 16 */
CHECK(test_rejects() == 0);          /* float tag 3, 3 channels, rate 0, rate 65536,
                                        zero-length data, missing fmt */
```

Each case writes into `test_enc_wav_tmp.wav`, loads it, checks `rate`, `channels`, `sample_count`, and sample values, then removes the file. `test_rejects` asserts `bink2_enc_wav_load(...) == -1`, `err[0] != '\0'`, and `pcm == NULL`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake -S . -B build -DBINK2_BUILD_TOOLS=ON && cmake --build build --target test_enc_wav`
Expected: build failure, `bink2_enc_wav.h` not found.

- [ ] **Step 3: Implement the loader**

Walk RIFF chunks from the first one after the 12-byte header. Reject any chunk body larger than the remaining file. Advance by `size + (size & 1)`. Accept the first `fmt ` and first `data` chunk; ignore others. Require PCM tag 1, channels 1 or 2, bits 8/16/24, rate 1..65535, and at least one sample frame. Convert 8-bit unsigned by `(value - 128) << 8`; 16-bit little-endian via sign extension; 24-bit by `(value + 128) >> 8` for non-negative and `-((-value + 128) >> 8)` for negative, clamped to `[-32768, 32767]`.

- [ ] **Step 4: Wire the build and run the test**

Add `src/encoder/bink2_enc_wav.c` to `BINK2_ENCODER_SOURCES`. Add:

```cmake
add_executable(test_enc_wav test_enc_wav.c)
target_link_libraries(test_enc_wav PRIVATE bink2_encoder)
add_test(NAME enc_wav COMMAND test_enc_wav)
```

Run: `cmake --build build && ctest --test-dir build -R '^enc_wav$' --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/encoder/bink2_enc_wav.c src/encoder/bink2_enc_wav.h \
  tests/test_enc_wav.c CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: load PCM WAV audio for the encoder"
```

---

### Task 2: Audio state, forward DCT, and one-block round trip

**Files:**
- Create: `src/encoder/bink2_enc_audio.h`
- Create: `src/encoder/bink2_enc_audio.c`
- Create: `tests/test_enc_audio.c`
- Modify: `CMakeLists.txt` (`BINK2_ENCODER_SOURCES`)
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `bink2_audio_describe` and `bink2_audio_decode` from `src/bink2_audio.h`; `bink2_bits.h`.
- Produces:

```c
typedef struct Bink2EncAudio Bink2EncAudio;

int bink2_enc_audio_open(Bink2EncAudio **out, unsigned long rate,
                         int channels, unsigned long sample_count,
                         unsigned long fps_num, unsigned long fps_den,
                         unsigned long frame_count, int quality,
                         unsigned long *max_decoded);
void bink2_enc_audio_close(Bink2EncAudio *audio);
int bink2_enc_audio_frame(Bink2EncAudio *audio,
                          int (*read_audio)(void *user,
                                            unsigned long start_sample,
                                            unsigned long sample_count,
                                            short *dst),
                          void *user, int is_last,
                          unsigned char **payload, size_t *payload_size,
                          size_t *pcm_bytes);
```

`bink2_enc_audio_open` validates channels 1..2, rate 1..65535, `sample_count > 0`, and nonzero frame rate/count. It calls `bink2_audio_describe(rate, channels, 1, 1, &info)` and rejects if that fails. `max_decoded` is the video-duration sample count plus one `block_out`, times channels times 2. The callback and user pointer keep this module independent of the public source struct until Task 5 wires them together.

- [ ] **Step 1: Write the failing test**

Create `tests/test_enc_audio.c`. Add a tone buffer and a `read_audio` callback that copies interleaved s16 from a file-scope pointer, clamped to the sample count.

Test one 48 kHz stereo block by opening with `rate=48000`, `channels=2`, `sample_count=1920`, `fps_num=25`, `fps_den=1`, `frame_count=1` (target 1920 = `block_out`), `quality=80`, and calling `bink2_enc_audio_frame` with the tone callback and its context. After the call:

```c
CHECK(payload != NULL);
CHECK(payload_size > 4);
CHECK(pcm_bytes == 1920UL * 2UL * 2UL);
CHECK(payload[0] == (pcm_bytes & 0xFF));   /* reported decoded size */
CHECK(bink2_audio_create(&dec, 48000, 2, 1, 1, max_decoded) == 0);
CHECK(bink2_audio_decode(dec, payload, payload_size) == 0);
CHECK(bink2_audio_pcm_bytes(dec) == pcm_bytes);
CHECK(snr_db(decoded, source, 128 * 2, 1920 * 2) >= 25.0);
```

`snr_db` skips the first 128 sample frames (the overlap fade-in), sums squared signal and error, and returns `10*log10(signal/error)`. Add an all-zero source case: decoded samples are all 0. Add a constant-DC case: SNR >= 25 dB.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake -S . -B build -DBINK2_BUILD_TOOLS=ON && cmake --build build --target test_enc_audio`
Expected: build failure, `bink2_enc_audio.h` not found.

- [ ] **Step 3: Implement the state and transform**

Implement `Bink2EncAudio` with:

```c
typedef struct {
    int frame_len;
    double *re;
    double *im;
} Bink2EncDct;
```

`dct_forward` zero-extends `frame_len` input samples to `2*frame_len`, runs the existing radix-2 FFT, and for `k` in `[0, frame_len)` computes `re[k]*cos(pi*k/(2n)) + im[k]*sin(pi*k/(2n))`.

`bink2_enc_audio_write_block` writes the 2 zero bits, then per channel: forward DCT; `coef[0] * 0.5`; two 29-bit floats (`coef[0]/root`, `coef[1]/root`) using sign, 5-bit `frexpf` exponent, 23-bit rounded mantissa; 7-bit quantiser indices per band; coefficient groups from index 2.

Band quantiser selection for Task 2: `target = 8 << (quality * 10 / 100)`, clamped to 32767. For each band take the largest `|coef|` above index 2 and choose the smallest index `q` in 0..95 with `peak / quant[q] <= target`. Compute a global floor the same way from the loudest AC coefficient, subtract 12, clamp to 0, and use `max(band_q, floor_q)`.

Coefficient groups: at index `i`, if an all-zero span from the RLE table fits, write `1`, the 4-bit entry, and a zero 4-bit width; otherwise write `0`, the 4-bit width, each magnitude with the band's quantiser, and a sign bit for each nonzero magnitude. Clamp a magnitude to 32767 and use width 15 if it overflows; this is the 0 dBFS case in Review Focus 1, and the first implementation should print a warning under `verbose` only.

`bink2_enc_audio_frame` allocates a `Bink2BitWriter`, writes the 32-bit decoded size, writes each block, aligns 32 bits after every block, and copies the writer bytes into a `malloc` payload. `pcm_bytes = blocks * block_out * channels * 2`.

- [ ] **Step 4: Wire the build and run the test**

Add `src/encoder/bink2_enc_audio.c` to `BINK2_ENCODER_SOURCES`. Add:

```cmake
add_executable(test_enc_audio test_enc_audio.c)
target_link_libraries(test_enc_audio PRIVATE bink2_encoder)
add_test(NAME enc_audio COMMAND test_enc_audio)
```

Run: `cmake --build build && ctest --test-dir build -R '^enc_audio$' --output-on-failure`
Expected: PASS. If the SNR is below 25 dB, fix the transform or quantiser scaling; do not lower the floor.

- [ ] **Step 5: Commit**

```bash
git add src/encoder/bink2_enc_audio.c src/encoder/bink2_enc_audio.h \
  tests/test_enc_audio.c CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: encode Bink Audio 2 DCT blocks"
```

---

### Task 3: Frame scheduling, quality, stereo, and writer/reader agreement

**Files:**
- Modify: `src/encoder/bink2_enc_audio.c`
- Modify: `tests/test_enc_audio.c`

**Interfaces:**
- Consumes: the Task 2 functions unchanged.
- Produces: no new public names. `bink2_enc_audio_frame` now handles multiple blocks per frame and any frame count.

- [ ] **Step 1: Extend the tests**

Add a 3-frame 48 kHz stereo case at `fps 24/1` (targets 2000, 4000, 6000 samples; block counts 2, 1, 1). Decode each chunk with one `Bink2Audio` decoder, accumulate `pcm_bytes`, and check the sum equals the decoded total and covers 6000 sample frames plus at most one block.

Add a syntax parser in the test using `Bink2Bits` that follows the block grammar in the design doc for one chunk. After every block assert the bit position is 32-bit aligned and the padding bits read as zero; at the end assert `bits.bitpos == payload_size * 8`.

Add rate-class cases at 16000 mono, 32000 mono, and 48000 stereo, each opened with `fps_num=rate`, `fps_den=block_out` so one frame emits exactly one block. Check the decoded byte count reveals `block_out` 480, 960, and 1920 respectively, and SNR >= 25 dB after the overlap skip. Add threshold cases at 22049, 22050, 44099, and 44100 with one frame each; their decoded block sizes must be 480, 960, 960, and 1920 samples per channel. These pin the `frame_len` boundaries.

Add quality monotonicity over 1 second of the same tone: total payload bytes at quality 20 are <= quality 80, and quality 0 decodes without error. Add 1 second of silence and assert its payload is at least 4x smaller than the tone at the same quality.

Add a 0 dBFS 20 kHz sine at 48 kHz stereo: encoding succeeds, decoding succeeds, and SNR >= 8 dB after the overlap skip.

- [ ] **Step 2: Run to verify the new cases fail**

Run: `cmake --build build && ctest --test-dir build -R '^enc_audio$' --output-on-failure`
Expected: FAIL, because the scheduler still emits one block and the parser reaches the wrong end.

- [ ] **Step 3: Implement scheduling**

Add `acc`, `target`, `produced` (all `uint64_t`), `source_pos`, `sample_count`, and `block_out`. For each frame:

```c
acc += rate * fps_den;
target += acc / fps_num;
acc %= fps_num;
while (produced < target) {
    produced += block_out;
    blocks++;
}
```

Ignore `is_last` for duration: the source is truncated at the video target. Read `block_out` samples at `source_pos` and `overlap_len` lookahead at `source_pos + block_out`, clamping both to `sample_count` and zero-filling the rest. Advance `source_pos` by `block_out` per block. Reject if the decoded size or payload size exceeds `UINT32_MAX`.

- [ ] **Step 4: Run the tests**

Run: `cmake --build build && ctest --test-dir build -R '^enc_audio$' --output-on-failure`
Expected: PASS. Record the measured SNR values in a comment beside the thresholds.

- [ ] **Step 5: Commit**

```bash
git add src/encoder/bink2_enc_audio.c tests/test_enc_audio.c
git commit -m "feat: schedule multiple audio blocks per video frame"
```

---

### Task 4: Container track metadata

**Files:**
- Modify: `src/encoder/bink2_enc_container.h`
- Modify: `src/encoder/bink2_enc_container.c`
- Modify: `src/encoder/bink2_encoder.c` (pass `NULL, 0` for now)
- Create: `tests/test_enc_container_audio.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:

```c
typedef struct {
    unsigned long rate;
    int channels;
    unsigned long max_decoded;
    unsigned long id;
    int use_dct;
} Bink2EncAudioTrack;

int bink2_container_write_header(FILE *file, unsigned long width,
                                 unsigned long height, unsigned long fps_num,
                                 unsigned long fps_den,
                                 unsigned long frame_count,
                                 unsigned long flags,
                                 const Bink2EncAudioTrack *tracks,
                                 unsigned long track_count,
                                 unsigned long *index_offset);
```

- [ ] **Step 1: Write the failing test**

Create `tests/test_enc_container_audio.c` using `tmpfile()`. Write a 1-track header with rate 48000, stereo, `max_decoded=12345`, id 1. Read the file back and assert:

```c
CHECK(le32(header + 40) == 1);
CHECK(le32(header + 44) == 0x87e70800UL);
CHECK(le32(header + 48) == 12345);
CHECK(header[52] == 0x80 && header[53] == 0xBB && header[54] == 0x00);
CHECK(header[55] == 0x30);                 /* DCT | stereo */
CHECK(le32(header + 56) == 1);
CHECK(index_offset == 60);
```

Add a zero-track case: index offset 48 and no metadata. Add a mono 44100 case: flags `0x10`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target test_enc_container_audio`
Expected: build failure, because the signature has no track arguments.

- [ ] **Step 3: Implement the header**

Write the fixed 48 bytes as today, replacing the zero track count with `track_count`. Then for each track write `max_decoded`, then the descriptor (rate low, mid, high bytes, then `(use_dct ? 0x10 : 0) | (channels == 2 ? 0x20 : 0)`), then each track id. Set `index_offset = 48 + 12 * track_count` and write the zero index entries there.

- [ ] **Step 4: Update the caller and run**

Pass `NULL, 0` from `bink2_enc_encode_intra`. Add the test target and run:

`cmake --build build && ctest --test-dir build -R '^enc_container_audio$' --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/encoder/bink2_enc_container.c src/encoder/bink2_enc_container.h \
  src/encoder/bink2_encoder.c tests/test_enc_container_audio.c tests/CMakeLists.txt
git commit -m "feat: write audio track metadata in the KB2j header"
```

---

### Task 5: Public source API and the encode loop

**Files:**
- Modify: `src/encoder/bink2_encoder.h`
- Modify: `src/encoder/bink2_encoder.c`
- Modify: `tests/test_enc_api.c`
- Create: `tests/test_enc_audio_file.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Tasks 2–4.
- Produces: the public `bink2_enc_audio` struct, `source.audio`, `source.read_audio`, and `options.audio_quality`.

- [ ] **Step 1: Extend the API test**

In `tests/test_enc_api.c`:

```c
bink2_enc_options_default(&o);
CHECK(o.audio_quality == 80);
```

Add `REJECT` cases: `audio.channels = 3`, `audio.channels = 1` with `audio.rate = 0`, `audio.channels = 1` with `audio.sample_count = 0`, `audio.channels = 1` with `read_audio = NULL`. Add `REJECT_OPT(audio_quality, -1)` and `REJECT_OPT(audio_quality, 101)`. The existing `valid_source` must keep `audio.channels = 0`.

Create `tests/test_enc_audio_file.c`: encode two 64x64 frames at `fps 25/1` with 48000 Hz stereo audio. Run two subcases: 4800 source samples (truncated to the 3840-sample video duration) and 1000 source samples (padded to 3840). For both, enable `verify`. Reopen:

```c
CHECK(bink2_info_audio(b, &mask, channels, depths, rates) == 0);
CHECK((mask & 1) != 0 && channels[0] == 2 && depths[0] == 16 &&
      rates[0] == 48000);
CHECK(bink2_enable_audio(b, 0, 1) == 0);
/* decode both frames and sum bink2_get_audio_size(b, 0) */
CHECK(total == 2UL * 1920UL * 2UL * 2UL);
```

For the padded case, also check the samples after the 1000th source frame are all zero. Encode the same two frames without audio and compare the index entries: each audio frame's offset delta must be larger than the video-only delta, proving the length prefix and chunk are inside the packet. Also read the header's largest-packet field at offset 12 and assert it is at least the largest index delta.

Also encode a source with `audio.channels = 0` and assert `bink2_info_audio` reports no track.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target test_enc_api test_enc_audio_file`
Expected: build failure, unknown `audio_quality` / `audio`.

- [ ] **Step 3: Implement the API and encode loop**

Add the public struct, fields, and `audio_quality` exactly as in the design doc, and default it to 80. In `valid_source`, require `channels` 0/1/2; when nonzero require rate 1..65535, `sample_count > 0`, and `read_audio != NULL`. In `valid_options`, require `audio_quality` 0..100.

In `bink2_enc_encode_intra`, after option validation:

1. If `source->audio.channels != 0`, call `bink2_enc_audio_open` with the source audio fields, frame rate/count, and `options->audio_quality`, and fail with `BINK2_ENC_ERR_INPUT` if it fails. Do this before `bink2_container_write_header`. Fill a `Bink2EncAudioTrack` and pass it to the header writer; otherwise pass `NULL, 0`.
2. Each frame: encode video, then call `bink2_enc_audio_frame(audio, source->read_audio, source->user, is_last, ...)` when audio is present. Write `u32 audio_size`, the audio payload, then the video bytes. The frame offset is the position before the length field, and `largest` includes all of it.
3. Track video bytes separately for `target_bitrate` adjustment.
4. Accumulate `pcm_bytes`; pass the total to verification and require the decoded total to match exactly.
5. Close the audio state on every exit path.

In `verify_written`, accept `channels`, `rate`, `expected_pcm_bytes` (0 when no audio). When audio is expected, enable track 0 after opening, check metadata, decode every frame, and compare the total `bink2_get_audio_size` sum to `expected_pcm_bytes`.

- [ ] **Step 4: Run the tests**

Run: `cmake --build build && ctest --test-dir build -R '^(enc_api|enc_audio_file)$' --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/encoder/bink2_encoder.h src/encoder/bink2_encoder.c \
  tests/test_enc_api.c tests/test_enc_audio_file.c tests/CMakeLists.txt
git commit -m "feat: add audio sources to the encoder API"
```

---

### Task 6: `bink2_build --audio`

**Files:**
- Modify: `src/tools/bink2_build.c`
- Modify: `tests/test_build_e2e.c`
- Modify: `docs/tools.md` (option list; full prose comes in Task 7)

**Interfaces:**
- Consumes: `bink2_enc_wav_load`, `source.audio`, `source.read_audio`, `audio_quality`.
- Produces: CLI options `--audio FILE` and `--audio-quality N`.

- [ ] **Step 1: Extend the end-to-end test**

In `tests/test_build_e2e.c`, write `build_e2e.wav` with 48000 Hz stereo, 16-bit, and 9600 sample frames of a 1 kHz tone. Change the command to:

```c
"... --audio build_e2e.wav --audio-quality 60 --verify"
```

After reopening, check one track with 48000 Hz stereo, enable it, decode all three frames, and assert the total PCM equals `9600 * 2 * 2` bytes. Remove the WAV at the end.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target test_build_e2e && ctest --test-dir build -R '^build_e2e$' --output-on-failure`
Expected: FAIL, `bink2_build` rejects `--audio`.

- [ ] **Step 3: Implement the tool**

Add `--audio FILE` and `--audio-quality N` to the parser and `usage()`. Keep a `Bink2EncWav` in the tool state; load it after the image input and before encoding. Add a `read_audio` callback that copies `count * channels` shorts from `wav.pcm + start * wav.channels` and zero-fills any short request. Set `source.audio.rate`, `channels`, and `sample_count`; set `source.read_audio`; set `options.audio_quality`. Free the WAV on every exit. Reject `--audio-quality` without `--audio` only if you also update the usage text; otherwise allow it and document that it is ignored.

- [ ] **Step 4: Run the tests**

Run: `cmake --build build && ctest --test-dir build -R '^(build_help|build_e2e)$' --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/tools/bink2_build.c tests/test_build_e2e.c docs/tools.md
git commit -m "feat: add --audio and --audio-quality to bink2_build"
```

---

### Task 7: Malformed audio input and documentation

**Files:**
- Modify: `tests/test_malformed.c`
- Modify: `tests/test_enc_audio_file.c`
- Modify: `docs/encoder.md`
- Modify: `docs/api.md`
- Modify: `docs/tools.md`
- Modify: `README.md`
- Modify: `tests/README.md`
- Modify: `docs/README.md` (link this design doc)

**Interfaces:**
- Consumes: everything above.
- Produces: no code interfaces.

- [ ] **Step 1: Add the corruption cases**

In `tests/test_malformed.c`, using `build_video_ex` and the existing `test_audio_overrun` pattern, add:

```c
CHECK(test_audio_truncated_prefix() == 0);  /* chunk length 3 */
CHECK(test_audio_reported_size_overrun() == 0); /* chunk 4, u32 0xFFFFFFFF */
CHECK(test_audio_zero_rate_descriptor() == 0);  /* open returns NULL */
```

The first two build a frame with no video after the chunk and assert `bink2_first` returns `BINK2_ERROR` while `bink2_get_audio_size(b, 0) == 0`. The third edits the descriptor rate bytes (offset 48 in a `KB2g` header with one track) to zero and asserts `bink2_open_memory(...) == NULL`.

In `tests/test_enc_audio_file.c`, take a valid encoded file, locate the first frame offset from the index (offset 60 with one track), and overwrite the first four bytes of the audio chunk (the reported decoded size) with `0xFFFFFFFF`. Reopen, enable video and audio, and assert the first video frame still returns `BINK2_OK` while `bink2_get_audio_size(b, 0) == 0`.

- [ ] **Step 2: Run the malformed test**

Run: `cmake --build build && ctest --test-dir build -R '^(malformed|enc_audio_file)$' --output-on-failure`
Expected: PASS.

- [ ] **Step 3: Update the docs**

- `docs/encoder.md`: remove "no audio"; add a short audio section covering one track, WAV formats, rate limit, the lossy guarantee, the quality option, and the block syntax reference to the design doc. Add `audio_quality` to the options table.
- `docs/api.md`: document `bink2_enc_audio`, `source.audio`, `read_audio` (interleaved s16, repeatable, range), and `audio_quality`, including `channels = 0`.
- `docs/tools.md`: `--audio`, `--audio-quality`, accepted WAV formats, truncation/padding, no resampling.
- `README.md`: status bullet and option table mention audio.
- `tests/README.md`: the optional `test_enc_audio_oracle` command and its `BINK2_ORACLE` variable.
- `docs/README.md`: add `audio-encoder-design.md` to the index.

- [ ] **Step 4: Run the release suite**

Run: `cmake --build build && BINK2_TEST_MEDIA="$HOME/Downloads" ctest --test-dir build --output-on-failure`
Expected: all tests pass; reference tests pass when the media directory holds the samples, and skip otherwise. Record the exact counts.

- [ ] **Step 5: Commit**

```bash
git add tests/test_malformed.c tests/test_enc_audio_file.c docs README.md \
  tests/README.md
git commit -m "docs: document encoded audio and malformed audio cases"
```

---

### Task 8: Optional oracle comparison

**Files:**
- Create: `tests/test_enc_audio_oracle.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: the public encoder and decoder APIs.
- Produces: an optional test named `enc_audio_oracle`.

- [ ] **Step 1: Write the test**

If `getenv("BINK2_ORACLE")` is NULL or empty, return 77. Otherwise encode a 48 kHz stereo, 24 fps, 24-frame tone into `enc_audio_oracle.bk2` with `bink2_enc_encode`. Decode the file twice:

- with this library: `bink2_open_file`, enable audio track 0, step every frame, append `bink2_get_audio(b, 0)` to `ours.s16`;
- with the oracle: `system("\"<oracle>\" -v error -i enc_audio_oracle.bk2 -map 0:a:0 -f s16le ours_oracle.s16")`.

Read both files fully. Assert the sizes are equal and `memcmp` is 0. Compute SNR against the source tone over the first 24 * 2000 sample frames, skipping the first 128 frames, and assert >= 25 dB. Remove all temporary files and return 0. Return 1 if the oracle exits nonzero.

- [ ] **Step 2: Add the test target**

```cmake
add_executable(test_enc_audio_oracle test_enc_audio_oracle.c)
target_link_libraries(test_enc_audio_oracle PRIVATE bink2_encoder)
add_test(NAME enc_audio_oracle COMMAND test_enc_audio_oracle)
set_tests_properties(enc_audio_oracle PROPERTIES SKIP_RETURN_CODE 77)
```

- [ ] **Step 3: Run it both ways**

Run: `cmake --build build && ctest --test-dir build -R '^enc_audio_oracle$' --output-on-failure`
Expected: SKIPPED without `BINK2_ORACLE`.

Run with the pinned oracle:

```sh
BINK2_ORACLE="$PWD/build/oracle/FFmpeg-bink2/ffmpeg" \
  ctest --test-dir build -R '^enc_audio_oracle$' --output-on-failure
```

Expected: PASS with byte-identical PCM.

- [ ] **Step 4: Commit**

```bash
git add tests/test_enc_audio_oracle.c tests/CMakeLists.txt
git commit -m "test: compare encoded audio with the pinned oracle"
```

---

### Task 9: Full verification

**Files:** none.

- [ ] **Step 1: Run the sanitizer suite**

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DBINK2_BUILD_TOOLS=ON \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan
BINK2_TEST_MEDIA="$HOME/Downloads" ctest --test-dir build-asan --output-on-failure
```

Expected: every test passes, with only the documented headless player skips. Fix anything the sanitizer reports before proceeding.

- [ ] **Step 2: Run the release suite and the oracle**

```sh
BINK2_TEST_MEDIA="$HOME/Downloads" ctest --test-dir build --output-on-failure
BINK2_ORACLE="$PWD/build/oracle/FFmpeg-bink2/ffmpeg" \
  ctest --test-dir build -R '^enc_audio_oracle$' --output-on-failure
```

Expected: all pass; oracle PCM matches.

- [ ] **Step 3: Check the tree**

Run: `git status --short && git log --oneline -10`
Expected: clean tree, one commit per task.

- [ ] **Step 4: Report**

Report the test counts, the skipped tests, the measured SNR values, and the resulting file sizes at low/default quality. Do not call the audio output bit-exact.
