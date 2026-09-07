# v0.2 Performance improvements and bitrate settings

[English README](../README.md) | [한국어](ko/performance-v0.2.md)

2026-09-06, RTX 5080 / NVIDIA 616.56 / FFmpeg 8.0, Windows Release build.

## Implementation changes

- Removed the full pre-decode with `ffprobe -show_entries frame=...`. Conversion prepares immediately after reading stream headers. Header frame counts/duration are progress estimates only; processing continues to normal EOF.
- The conversion decoder's `showinfo=checksum=0` validates frame index, PTS, timebase, dimensions, progressive state, pixel format, and color tags in sequence. Missing metadata or a discontinuous timeline stops conversion without publishing a final file. No extra decode is performed for these checks.
- D3D11VA is the default decoder. `hwdownload` retrieves P010/NV12. Hardware-path failure stops conversion; `--software-decode` is available for explicit comparison.
- Moved HDR RGB10 → BT.2020 limited P010 conversion to a D3D11 compute shader. It preserves the original 2×2 centered chroma and quantization rules without applying PQ/gamut conversion twice. Normal conversion no longer reads RGB back to the CPU.
- Reuse the P010 vector instead of allocating one per frame. Increased the requested process-pipe buffer to 4 MiB.
- Removed full output decoding from normal completion. The engine checks successful encoder exit and output HEVC Main10/HDR tags/dimensions. Full re-decoding and frame-count validation are available through `--verify-full` or independent development scripts.
- Raw frame samples and HDR comparison dumps are saved only with `--diagnostics`. Normal runs retain diagnostic JSON and text logs.
- `result.json` records startup latency, average conversion FPS, read/process/write times, and the quality settings actually used.

## Bitrate selection

After launching by double-click or drag and drop, enter a value such as `40M` in the console. Pressing Enter uses the default CQ 18.

```powershell
RTXVideoHDRConvert.exe "input.mp4" --output "video.hdr.mkv" --bitrate 40M
RTXVideoHDRConvert.exe "input.mp4" --output "quality.hdr.mkv" --cq 18
```

`40M` means 40,000,000 bit/s; `40000k` is equivalent. The accepted range is 100k..1000M; settings unsupported by the hardware/codec produce an encoder error. `--bitrate` is a VBR average target, so actual bitrate varies by scene. CQ ranges from 0..51, with lower values targeting higher quality/larger files. Defaults remain CQ 18 and NVENC p5/hq; quality settings were not lowered to improve speed. The two modes cannot be selected together. 40M is an example, not a recommendation for every video.

## Measurements

| Input/settings | Frames | Average conversion FPS | Time to first frame |
|---|---:|---:|---:|
| Extracted 4K 71.928 fps segment, GPU color, CQ 18, diagnostic samples enabled | 432 | 36.62 fps | 1.43 s |
| Direct input of the original 23-minute file, initial segment, GPU color, VBR 40M | 432 | 39.17 fps | 1.18 s |
| Same excerpt, hardware decode + original CPU color conversion, CQ 18 | 48 | 3.78 fps | Separate comparison run |

These are short-segment averages, not guaranteed full-length throughput. Time to first frame includes HDR effect checks and tool initialization. Average FPS covers frame processing and excludes final audio copy/container completion. The CPU comparison used a different frame count, so no exact matched-condition speedup is claimed. Actual results confirm a substantial reduction of the earlier roughly 3 fps bottleneck.

Measurement records:

- `artifacts/rtxhdr-run-102012-287938437/result.json` — 4K CQ 18, 432 frames.
- `artifacts/rtxhdr-run-105352-288098140/result.json` — original file directly, 40M, 432 frames.
- `artifacts/rtxhdr-run-81192-287965265/result.json` — CPU color comparison.

## Validation

- Independent `tools/verify-conversion.py`: full decoding of the 4K 432-frame output confirmed count and PTS; Main10/BT.2020/PQ/limited/center tags were correct. All 284 audio packets were byte-identical. Maximum timestamp errors were 0.333 ms for audio and 0.500 ms for video.
- 1080p 8-bit software-decoded input processed all 240 frames to EOF without a frame limit and passed `--verify-full`. Independent checks confirmed all 376 audio packets matched, white-patch median error was 0, and the maximum zscale Y comparison error was one code value.
- GPU P010 diagnostic samples matched the CPU reference on the same RGB input within one code value. This is color-conversion error, separate from lossy HEVC compression error.
- `GpuColorTests`: across eight combinations of 1918/1920×1080, 8/10-bit input, and gray ramp/color bars, GPU/CPU P010 error was 0 and the low six bits were 0.
- Processing the entire extracted fixture with a timestamp discontinuity failed precisely at frame 575. It was detected without a pre-scan, and no final file was created.
- CTest `file_paths` passed long-path, Unicode, and overwrite-prevention regression checks.

The distributed `sample-hdr.mkv` was a newly converted, roughly six-second CQ 18 result from this GPU path.

## Architecture referenced from NVEncC and next steps

Reviewed `initPipeline`, `allocatePiplelineFrames`, and `RunEncode2` in [NVEncCore.cpp](https://github.com/rigaya/NVEnc/blob/master/NVEncCore/NVEncCore.cpp). Its connected processing stages, reusable buffers, asynchronous depth management, and per-stage timing informed the design. NVEncC source was not copied, and this project was not integrated into NVEncC.

This change adds hardware decoding, GPU color conversion, buffer reuse, live PTS validation, and stage timing. FFmpeg still runs in separate processes: decoded frames are downloaded to CPU memory and uploaded to D3D11, then HDR P010 is read back and delivered to the encoder.

The next performance steps are: (1) accept libavcodec D3D11 hardware frames directly to eliminate upload, (2) register D3D11 P010 textures directly with NVENC to eliminate output readback/pipes, and (3) overlap decode/HDR/encoding with bounded multi-frame buffers and GPU completion events. An entirely GPU-memory path was not implemented at this stage. The measured region including GPU processing and readback accounted for the largest share of time.
