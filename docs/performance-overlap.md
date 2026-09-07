# Overlapping frame processing: local performance validation

[English README](../README.md) | [한국어](ko/performance-overlap.md)

2026-09-06. Applied to a local test build without creating a new GitHub release at that time.

## Changes

Previously, input reading → waiting for HDR/P010 GPU processing → writing to the encoder pipe happened sequentially. GPU submission and collection are now separate. While the GPU processes the current frame, the next input is read; after submitting the next GPU operation, the previous output is sent to the encoder. Only one GPU frame remains pending, existing buffers are reused, and the D3D11 context stays on a single thread.

At EOF, the last pending frame is collected and delivered. Progress counts frames fully written to the encoder pipe. Input validation stays in its original order. GPU wait timeouts and encoder failure diagnostics are preserved.

The GUI enables this by default in this build. Use `--serial-pipeline` to compare with the original sequential path. `--cpu-color` and `--diagnostics` for raw samples use the sequential path. Check `overlapped_pipeline` in `result.json` to see which mode ran. Timing fields measure time spent by the calling thread, not pure GPU execution time.

## Repeated comparison on the same segment

RTX 4060 (adapter 1), first 432 frames of 3840×2160, 10-bit SDR, 71.928 fps input. CQ 18, HEVC Main10, p5/hq, MKV. Overlapped and sequential runs alternated each round. Other user jobs were left running, so this was not a fully isolated benchmark.

| Round | Sequential | Overlapped |
|---|---:|---:|
| 1 | 22.98 fps | 35.09 fps |
| 2 | 22.88 fps | 35.68 fps |
| 3 | 20.94 fps | 35.47 fps |
| Median | 22.88 fps | 35.47 fps |

Median throughput increased by about 55%, reducing processing time for the same frame count by about 35%. These short-segment results do not guarantee full-length or other-GPU performance. Encoding quality settings and HDR/color formulas were unchanged. CPU↔GPU copies remained; direct GPU-texture decoding/NVENC integration was outside this change.

Local measurement logs are `artifacts/overlap-enabled-{1,2,3}.txt` and `artifacts/overlap-serial-{1,2,3}.txt`. Earlier `overlap-baseline-*` measurements from a backup of the original implementation were excluded because timing and load conditions differed.

## Completed validation

- `tools/verify-overlap.py`: full decoding of both 4K 432-frame outputs produced identical framemd5 and timestamps. Main10/BT.2020/PQ/limited/center tags, audio packet hashes, and audio timing matched.
- `GpuColorTests`: eight combinations of 1918/1920×1080, 8/10-bit, and gray ramp/color bars had maximum CPU-reference error 0. P010 bytes matched before and after separating submit/collect. Reusing a pending input and duplicate collection were refused.
- One- and two-frame conversions with `--verify-full` passed final-frame draining and frame-count checks.
- Actual GUI MKV/MP4 conversion to normal EOF at 240 frames, stage display, and cancellation passed.
- The discontinuous-timestamp fixture was rejected at frame 575 as before, without creating a final file.
- Three CTest cases passed: paths, FPS, and child-process failure diagnostics.

The local executable was built at `build-overlap/Release/RTXVideoHDR.exe` to avoid overwriting an existing running build. No GitHub upload or new release was made for this change at that time.
