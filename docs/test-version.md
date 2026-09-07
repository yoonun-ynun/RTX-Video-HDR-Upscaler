# First file-conversion test version

[English README](../README.md) | [한국어](ko/test-version.md)

## Implemented workflow

Choose one video or drop it onto the executable to save an HDR MKV beside the source. The CLI accepts input/output paths, adapter, FFmpeg location, and a maximum test frame count. NVEncC is not used.

The conversion path is FFmpeg decode → NV12 or P010 upload → NVIDIA D3D11 HDR extension → RGB10A2 readback → explicit BT.2020 NCL P010 conversion → FFmpeg NVENC HEVC Main10 → audio stream copy. This test version prioritizes correctness and permits CPU round trips. GPU-only decode/encode integration is a later optimization.

This version implements file input/encoding previously deferred in the specification and adds 10-bit SDR input for the supplied video. It does not reduce a 10-bit source to 8-bit NV12. The actual rational frame rate is passed to the D3D11 content descriptor and encoder.

## Decisions by stage

1. Added the NVIDIA extension after validating the baseline frame path.
2. Although the general SDR→PQ capability query returned false, enabling the extension on the supported BT.709 baseline VP configuration produced HDR output. The product path therefore does not depend on bypassing an unsupported combination.
3. The mean ON/OFF gray-ramp difference was about 88 code values. Under a PQ interpretation, ON white was about 990–999 nits, consistent with the queried display value of 1000 nits. Input primary and neutral-patch chromaticities were also checked. These observations alone do not establish color accuracy in every environment or browser equivalence.
4. The public renderer also treats the RTX HDR path as PQ/BT.2020 for presentation and PQ→SDR correction. These observations and measurements motivated the initial BT.2020/PQ output contract. [Renderer presentation path](https://raw.githubusercontent.com/Aleksoid1978/VideoRenderer/master/Source/DX11VideoProcessor.cpp)
5. Re-decoding the first file confirmed color tags, frame count, and preservation of GPU samples. NVENC left the chroma-location tag as left; the HEVC bitstream was corrected to center, matching the actual 2×2 average.
6. The actual user input was 3840×2160, 72000/1001 fps, 10-bit SDR, so a P010 input path was added. A full-length run of the 23-minute 36-second video was not tested. The first version was validated on a short segment of the real input.

## Validated synthetic video

- 1920×1080, 30 fps, eight seconds, 240 frames, AAC audio.
- HEVC Main10, yuv420p10le, BT.2020 / SMPTE ST 2084 / BT.2020 NCL, TV range, centered chroma.
- Mean re-decoded error on first/middle/last samples was about 0.52–0.54 code values. Median error on bright flat patches was 0.
- An independent FFmpeg zscale calculation matched the RGB→Y′ matrix. Automatically converting floating-point RGB to P010 introduced an intermediate conversion, so the independent reference used zscale's native planar 10-bit output.
- Data hashes matched for all 376 AAC packets. Audio/video timestamp errors were within about 0.333 ms, arising from MKV's millisecond timebase.
- Detailed evidence: `artifacts/first-test-hdr.mkv.run-47832-284764609/verification.json`.

## User-video test

- Read the supplied file and extracted a segment near 60 seconds using packet copy. Keyframe alignment means the excerpt did not start at exactly the requested 60 seconds. The source was unchanged.
- Simple packet cutting produced discontinuous timestamp intervals in the final B-frames. Rather than treating the full excerpt as constant frame rate, only the first 432 frames with verified continuity were converted using `--max-frames 432`.
- Output: **3840×2160, 72000/1001 fps (about 71.928 fps), 432 frames, about 6.006 seconds, HEVC Main10 HDR + AAC**.
- Mean re-decoded errors on first/middle/last frames were about 1.09 / 0.072 / 0.299 code values. Maximum errors at high-frequency edges were larger; this is not lossless output.
- RGB→Y′ values matched independent zscale calculations. All 284 AAC packets were preserved, with maximum timestamp errors of 0.333 ms for audio and 0.5 ms for video.
- SDR tone-mapped QA images confirmed expected frame content and color channels. They do not substitute for viewing the actual result on an HDR monitor.
- A separate one-frame run directly passing the original long file path to the executable also succeeded.
- This version processed 4K at roughly 3 fps in the tested environment, so a full 23-minute conversion could take considerable time.
- Detailed evidence: `artifacts/user-test-hdr.mkv.run-32704-285402203/verification.json`.
- Because MKV uses nanosecond default frame duration and a millisecond timebase, ffprobe reported the approximate rate `21003/292`. This differs from input `72000/1001` by about 0.000012 fps; actual per-frame timing remained within 0.5 ms of the original rational rate. Audio packet boundaries made the total container duration appear as 6.012 seconds.

## First-version limitations

Supported input is BT.709 limited 8/10-bit H.264/HEVC 4:2:0, progressive, with fixed dimensions and frame rate. Other inputs produce an explanation instead of silent misinterpretation. Missing color tags are not assumed to mean BT.709 without explicit user selection.

Encoding is lossy, so not all pixels match GPU raw samples. HDR static mastering metadata and MaxCLL/MaxFALL were not invented. Browser capture comparison, NVIDIA App slider effects, long-run stability, and varied HDR display configurations require further validation.

## Next improvements

After feedback on the result's brightness and color, improve speed using GPU RGB→P010 conversion and connected decode/encode resources. Later work includes a general UI, batch processing, VFR, non-square pixels, and additional color spaces. NVEncC integration is not planned.
