# Original sequence of next steps

[English README](../README.md) | [한국어](ko/next-steps.md)

The first test version of the standalone file converter was implemented. See the [test-version results](test-version.md) and README for behavior and validation. This document records the initial plan; it is not an instruction to restart completed work. At that point, the next product work was to incorporate feedback on HDR appearance and improve speed through the GPU path.

## 1. Execution sequence

| Order | Work | Deliverable | Condition for proceeding |
|---|---|---|---|
| 1 | Inspect GPU, Windows, driver, and tools | Environment report | Actual test environment confirmed |
| 2 | Create CMake C++20 x64 project and probe | Executable and capability JSON | G0 |
| 3 | NV12 pattern → VP OFF → raw dump | Input, output, sidecar | G1 |
| 4 | NVIDIA extension ON/OFF and repeated runs | Three-run comparison report | G2 |
| 5 | Investigate settings/HDR/offscreen dependencies | Operating-condition matrix | Reproducible constraints |
| 6 | Calibrate color/capture and compare | Color contract, waveform, comparison report | G3; browser equivalence judged separately |
| 7 | Short raw sequence → P010 → NVENC output | 10-second HEVC Main10 file | G4 |
| 8 | Connect FFmpeg D3D11VA decoding | SDR video conversion | Source frames/timeline preserved |
| 9 | Implement mux, audio copy, and draining | Playable MKV | G5 |
| 10 | Optimize direct RGB input and GPU queues | Performance/quality comparison | G6 |
| 11 | Prepare distribution and regression tests | Dependencies, usage, tested environments | Standalone tool reproducible |
| 12 | Finish the one-video-input → HDR-file workflow | Simple launch, progress, and error reporting | Standalone converter complete |

Step 7 tests only the encoder using a short raw sequence with verified colors. This order separates decode problems from encoding problems. Gate results, rather than implementation size, determine the next work.

## 2. Initial implementation scope

The first coding tasks, **1–3**, were completed through the SDR baseline path. Step 4 was to be added as a separate change. The first prompt below records that initial scope; later continuation should use section 4 together with actual execution results.

The following is the full target command interface from that planning stage. See the README for executable SDR baseline commands. At the time of this plan, `--hdr on`, raw input, and converter commands were not yet implemented. These historical examples are not a current CLI reference.

```powershell
RTXVideoHDRTest.exe probe --adapter 0 --report artifacts/probe.json
RTXVideoHDRTest.exe frame --pattern gray-ramp --size 1920x1080 --hdr off --frames 120 --out artifacts/off
RTXVideoHDRTest.exe frame --pattern gray-ramp --size 1920x1080 --hdr on --frames 120 --out artifacts/on
RTXVideoHDRTest.exe frame --input input.nv12 --size 1920x1080 --input-color bt709-limited --hdr on --out artifacts/raw
# Later converter
RTXVideoHDRConvert.exe --input input.mp4 --output output.mkv --encoder-input p010 --audio copy
```

Adapter 0 is an example. Choose an NVIDIA adapter from the actual probe output. Do not overwrite the source; use separate output paths for each run.

## 3. Initial prompt for a coding agent

```text
Read README.md, docs/implementation-spec.md, docs/validation-plan.md,
and docs/next-steps.md in this repository and perform the first implementation stage.

Scope:
1. Inspect the actual GPU, Windows, NVIDIA driver, MSVC, CMake, and Windows SDK.
2. Create the C++20/CMake x64 RTXVideoHDRTest project.
3. Implement probe to record NVIDIA adapter selection, required D3D11 video
   interfaces, NV12 input/R10G10B10A2 output, and the specified color-space
   conversion support in JSON.
4. Generate numerically defined BT.709 limited NV12 patterns.
5. Implement VideoProcessorBlt with the HDR extension OFF and save raw/JSON output.
6. Verify RowPitch, packed RGB10 channel order, and file size.
7. Run actual builds/GPU tests where possible and record commands and results.

Do not add NVIDIA extension ON, FFmpeg, NVENC, GUI, or NVEncC integration yet.
Do not report unsupported capabilities or unavailable GPU execution as success.
If installation is needed, first inventory existing tools and required components.
Report changed files, build/run results, G0/G1 decisions, and remaining blockers.
```

## 4. Prompt after the first stage

```text
Add the documented NVIDIA HDR extension adapter to the existing RTXVideoHDRTest.
Implement it independently using the public calling convention and record
ABI/payload/HRESULT details.
Run OFF/ON independently under the same color-space conditions and process
multiple patterns for 120 frames each.
Report three fresh-process repetitions, code-value statistics, reproducibility,
and the G2 decision.
Do not declare actual HDR application from S_OK alone. If inconclusive, follow
the documented failure branches.
Do not add HDR file encoding before G3 passes.
```

## 5. Final user workflow

The user selects one video and an output path. After checking input support, the program saves HDR video while preserving original resolution, frame rate, and audio. The default name adds `.hdr` to the source name and never overwrites the source.

Complete command-line conversion first, then add a drag-and-drop launcher or simple file picker. Display progress, estimated time remaining, and the completed path; explain why unsupported inputs cannot convert.

NVEncC integration is excluded. NVENC is a separate NVIDIA hardware encoding API and remains part of file output. “Upscaling” currently means SDR-to-HDR conversion; spatial enlargement such as 1080p→4K would require an additional request and design.
