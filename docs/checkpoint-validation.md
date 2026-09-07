# Checkpoint resume and diagnostics validation (v0.4.1)

[English README](../README.md) | [한국어](ko/checkpoint-validation.md)

Tested on 2026-09-06 on Windows with an NVIDIA RTX 4060. The GUI MKV/VBR test also used an RTX 5080. These results and the checkpoint toggle are included in v0.4.1.

## Verified behavior

| Test | Result |
|---|---|
| Segmented conversion: 1080p, 240 frames; 4K, 71.928 fps, 432 frames | Passed |
| Force termination after a checkpoint → manual resume | Passed; saved segment hashes were preserved |
| Compare uninterrupted segmented conversion with resumed output | Decoded frame bytes, timestamps, HDR tags, and audio packets matched |
| Force termination during MP4 audio muxing after video completion | Resumed successfully without reprocessing video |
| Create another file at the final path to force publication failure | Existing file preserved; muxed output reused after resolving the conflict |
| Resume a job whose final file was already saved | Recorded hash checked and completion recognized |
| Modify a saved segment / checkpoint body | Resume refused |
| Change modification time on a copy of the source | Resume refused; actual source left unchanged |
| Hold the job lock with another handle | Duplicate execution refused |
| Block checkpoint replacement to force a save failure | Last submitted and actually saved frame counts recorded separately; resume succeeded after unlocking |
| GUI cancel → close → resume in a new window | Completed a 300-second/9000-frame test video; source/output settings restored; gui-exit JSON created |
| GUI MKV/VBR, MP4/CQ, cancellation, and post-processing stage display | Passed |
| Disable GUI checkpoints → convert / cancel | Single-encoder option passed; no-resume notice shown; latest checkpoint location preserved |
| Change checkpoint preference → restart GUI | Selection retained; invalid values restored to the default enabled state |
| Resume an existing job with checkpoints disabled for new jobs | Completed 9000 frames while retaining the disabled preference for new jobs |
| Comparison pipe path, 30 frames | Passed |
| Existing CTest cases: paths, FPS, child processes | 3/3 passed |

Frame identity was compared **between conversions using the same checkpoint interval**. Single-encoder and segmented paths can differ in GOP boundaries and compressed output. These tests do not guarantee visual identity at HDR reinitialization boundaries for every type of content.

## Checkpoint overhead

A single measurement on the RTX 4060, processing the first 1800 frames of a 1080p 30 fps test input:

| Mode | Processing time | Processing FPS |
|---|---:|---:|
| `--no-checkpoint` | 4.119 s | 437.0 |
| Default saves every 10 seconds of video | 4.850 s | 371.2 |

FPS was about 15% lower in this test. Saving adds encoder finalization, disk synchronization, hashing, and decoder/encoder initialization for the next segment. Overhead varies with system load, GPU, resolution, and disk; this is not a general slowdown estimate. Increasing `--checkpoint-seconds` reduces save frequency but increases the segment that must be reprocessed after interruption. Resume validation reads saved video bytes to verify hashes and does not decode the entire source.

## Reproduction

```powershell
python tools/verify-checkpoint.py --input artifacts/first-test-sdr-tagged.mp4 --frames 240
python tools/verify-checkpoint.py --input artifacts/user-test-sdr.mp4 --frames 432
./tools/verify-gui.ps1 -BuildDirectory build-overlap
```

These input paths are local fixtures. Set `--input`, `--engine`, `--adapter`, and `--frames` for your environment. The tool writes outputs only under `artifacts/checkpoint-test-*` and does not modify the supplied source. The GUI relaunch test runs when the local fixture `artifacts/native-long-tagged.mp4` is available.
