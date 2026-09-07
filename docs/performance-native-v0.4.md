# v0.4.0 Direct GPU transfer validation

[English README](../README.md) | [한국어](ko/performance-native-v0.4.md)

## Implementation

The default path passes hardware-decoded textures from the same D3D11 device into HDR processing. A compute shader writes directly to the Y/UV planes of a P010 texture, and FFmpeg `hevc_nvenc` consumes that GPU surface. This removes the two CPU round trips for source frames and encoder input. Copies within GPU memory remain. NVENC uses eight surfaces and a four-frame delay. Default CQ/bitrate and p5/hq settings are unchanged.

`--pipe-video` selects the earlier overlapped input/GPU/output path; `--serial-pipeline` selects sequential processing. `--diagnostics`, `--cpu-color`, and `--software-decode` use the comparison pipe path. In-process failures on the direct path appear in `native.log`, `native-config.json`, and `error.json` messages.

## Verified results

- RTX 4060, 4K 10-bit SDR, 71.928 fps input, 432 frames, CQ 18: direct GPU transfer achieved about 96.7 fps. The three-run medians for the earlier sequential and overlapped pipe paths were 22.88 and 35.47 fps. Measurements reflect the system load and short segment at the time, not a guaranteed improvement for every GPU/video.
- Full decoding of all 432 frames from sequential and direct outputs produced identical pixel hashes and timestamps. HDR Main10/BT.2020/PQ/limited/center tags, audio packet hashes, and timing matched.
- A continuous five-minute 1080p 10-bit test video, 9000 frames, completed conversion and muxing and passed full output re-decoding. Frame processing took about 25.75 seconds (349.5 fps); final validation time was separate.
- One- and two-frame flush and complete frame-count checks passed. An input with a timestamp error at frame 575 was refused without creating a final file.
- Eight GPU color combinations (1918/1920 widths, 8/10-bit, ramp/color bars) produced byte-identical P010 in the earlier and direct-texture paths, with CPU-reference error 0.
- GUI MKV/VBR, MP4/CQ, stage display, and cancellation passed. Pre-install GUI guidance, the Windows PowerShell 5.1 installer, preservation of personal settings, and actual MP4 conversion/full validation using the installed package also passed.
- Three CTest cases passed: paths, FPS, and child-process error diagnostics.

A missing Matroska frame-rate declaration discovered during testing was fixed by explicitly recording `avg_frame_rate`/`r_frame_rate`. Final comparisons passed after that fix.

## Runtime and reproduction

FFmpeg shared libraries use a date-pinned BtbN 8.1.2 build. Source and SHA256 are recorded in `tools/ffmpeg-native-lock.json`. Releases do not include FFmpeg binaries; the installer downloads them from the official distribution and verifies them. All tested DLL hashes matched the pinned package.

Build: `tools/build.ps1 -FFmpegRoot <extracted-development-package> -BuildDirectory build-overlap`.

Pixel/timing/audio comparison: `python tools/verify-overlap.py <sequential.mkv> <direct.mkv> --frames 432`.

Hardware color validation: `build-overlap/Release/GpuColorTests.exe`.

Local evidence: `artifacts/native-fixed-rate-verify.txt`, `artifacts/native-long-tagged.txt`, `artifacts/rtxhdr-run-25180-11660109/result.json`, and `artifacts/packaged-native.txt`. Source videos, logs containing personal paths, and test outputs are not uploaded to GitHub.

## Global FFmpeg discovery

The v0.4.0 GUI searches the application folder and PATH. With global ffmpeg/ffprobe and shared DLLs available, GPU enumeration succeeded without prompting for installation. If only the executables are available, it acknowledges that FFmpeg was found and asks only for the GPU-processing DLLs. The Windows PowerShell installer was also verified to reuse existing global ffmpeg/ffprobe rather than reinstalling them.
