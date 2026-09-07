# Implementation plan and technical specification

[English README](../README.md) | [한국어](ko/implementation-spec.md)

Status on 2026-09-06: the first file-conversion version of this design was implemented. The initial implementation uses CPU round trips and external FFmpeg and supports 8/10-bit SDR input. See the [test-version document](test-version.md) for that version's support and validation results. This does not mean every long-term design below, including GPU-only integration, was completed at that point.

## 1. Goal and development principles

The goal is to process SDR frames on Windows through the NVIDIA driver's RTX Video HDR extension, validate the output color space, and save an HEVC Main10 file. A result resembling the browser's appearance is a comparison target, not currently guaranteed behavior.

The final product is a standalone converter that accepts one video and saves an HDR video file. It preserves source resolution and frame rate; spatial upscaling is outside the current scope. NVEncC integration is excluded. Implementation status and execution results are tracked in `stage1-results.md`.

Priorities are **confirm actual HDR processing → establish color interpretation → save files → complete the single-video workflow → improve performance**. CPU upload/readback is acceptable in the first prototype. Neither zero-copy processing nor an implementation within a few hundred lines is an initial completion requirement.

## 2. Confirmed facts and hypotheses to test

| Item | Current assessment | Implementation implication |
|---|---|---|
| NVIDIA-specific HDR extension call | Present in the public MPC Video Renderer source | Enables an independent experiment based on the calling convention |
| NVIDIA App sliders affect output | Unverified | Test identical inputs while varying settings |
| Offscreen processing without presentation | Unverified | First key validation target |
| A successful HRESULT means HDR was actually applied | Not guaranteed | Compare ON/OFF output and repeat-run variation |
| R10G10B10A2 output is always BT.2020/PQ | Cannot be determined from the storage format alone | Requires color-space settings and numerical validation |
| NVENC defines a 10-bit RGB input format | Confirmed in public headers | Device support and color conversion require separate testing |
| Driver and SDK paths use different models | Not established by current material | Separate observed output differences from internal implementation differences |

The MPC convention uses GUID `FDD62BB4-620B-4FD7-9AB3-1E59D0D544B3`, version 4, method 3, and an enable bit. Its presence in that source does not guarantee operation in a standalone application or driver compatibility. [MPC implementation](https://raw.githubusercontent.com/Aleksoid1978/VideoRenderer/master/Source/D3D11VP.cpp)

## 3. Scope

### Experimental v0.1

- Windows x64, C++20, CMake, MSVC, Windows SDK.
- An explicitly selected NVIDIA D3D11 hardware device.
- 1920×1080 progressive BT.709 limited NV12 test patterns and raw input.
- Extension ON/OFF, repeated frames, offscreen processing, raw dumps, and JSON diagnostics.
- GUI, audio, NVENC, and CUDA belong to later stages; NVEncC integration is out of scope.
- PNG input is optional. Start with numerically defined NV12 patterns to reduce uncertainty from sRGB/ICC handling and NV12 conversion.

### Standalone converter v0.2

- Initial supported input: SDR BT.709 limited, H.264/HEVC 8-bit 4:2:0, progressive, fixed resolution.
- Output: HEVC Main10, 10-bit 4:2:0, BT.2020/PQ/BT.2020 non-constant-luminance signaling; MKV first.
- Write HDR tags only after correct color conversion has been verified.
- Support audio stream copy and timeline preservation. Explicitly fail if the container cannot carry the codec.
- Reject HDR input, interlacing, midstream resolution changes, and ambiguous color metadata by default. Treat them as explicit future extensions.

## 4. Pipeline architecture

```text
Experiment:
Defined NV12 pattern/raw → D3D11 upload → VideoProcessor + NVIDIA extension
                                                       ↓
                                                R10G10B10A2 output
                                                       ↓
                                             Staging readback → raw + JSON

Video converter:
FFmpeg demux/decode (D3D11VA) → D3D11 NV12 texture
                                         ↓
                              Validated HDR processor
                                         ↓
                     R10G10B10A2 with established color interpretation
                         ↙                               ↘
          Validated GPU RGB→P010 conversion     Direct NVENC RGB input experiment
                         ↘                               ↙
                                   NVENC HEVC Main10
                                         ↓
                              FFmpeg mux + audio copy
```

Prefer D3D11VA for video decoding. FFmpeg CUDA/NVDEC output and D3D11 textures do not share the same resource contract, so interoperability must not be assumed. Handle the texture and array slice, device, and locking contract of D3D11VA frames. [FFmpeg D3D11 hardware context](https://ffmpeg.org/doxygen/trunk/hwcontext__d3d11va_8h_source.html)

## 5. File structure and module contracts

This is a proposed structure, not a description of already-existing implementation files.

```text
CMakeLists.txt
src/
  app/test_main.cpp              # probe/frame commands
  app/convert_main.cpp           # later convert command
  d3d/device_context.*           # adapter selection, device, diagnostics
  d3d/video_processor.*          # format checks, views, Blt
  d3d/nvidia_hdr_extension.*     # isolate extension ABI
  d3d/texture_readback.*         # synchronization and RowPitch handling
  color/color_contract.*        # primaries/transfer/matrix/range
  input/pattern_generator.*     # reproducible NV12 patterns
  io/raw_frame_writer.*         # packed raw + sidecar
  diagnostics/run_manifest.*    # environment, settings, results, errors
  media/ffmpeg_decoder.*        # later stage
  media/nvenc_encoder.*         # later stage
  media/muxer.*                 # later stage
  shaders/rgb_to_p010.hlsl      # later, if needed
tests/                         # CPU numerical checks and GPU integration tests
tools/                         # analysis tools
docs/
artifacts/                     # run outputs; planned Git exclusion
```

Core logical APIs have the following responsibilities. Exact signatures will follow the chosen RAII and error types during implementation.

| API | Input → output | Contract |
|---|---|---|
| `ProbeEnvironment` | Adapter selection → diagnostic report | Distinguish supported from unverified |
| `CreateProcessor` | Dimensions and input/output color contracts → processor | Check exact format/color-space combinations |
| `SetHdrEnabled` | bool → call status | Separate call status from evidence of actual application |
| `ProcessFrame` | texture/slice/PTS → output frame | Maintain input lifetime and expose output readiness |
| `ReadbackFrame` | Output frame → packed bytes | Wait for GPU completion and remove row padding |
| `EncodeFrame` | Validated frame → packets | Do not reuse resources before completion |

The frame contract includes texture, subresource, width/height, DXGI format, adapter LUID, PTS/timebase, color metadata, resource ownership, and completion state. A storage format such as `R10G10B10A2` must not be confused with a transfer function.

## 6. D3D11 requirements

1. Enumerate DXGI adapters and select by NVIDIA Vendor ID and the user's LUID/index. Do not silently fall back to WARP.
2. Use `D3D11_CREATE_DEVICE_VIDEO_SUPPORT`. Enable the debug layer optionally after checking its availability.
3. Record availability of `ID3D11VideoDevice`, `ID3D11VideoContext1`, and `ID3D11VideoProcessorEnumerator1`.
4. Create the processor with a progressive content descriptor containing dimensions and input/output frame rates.
5. Query NV12 input, R10G10B10A2 output, and the exact color-space conversion combination. Do not report unsupported combinations as successful. [Microsoft conversion support check](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11videoprocessorenumerator1-checkvideoprocessorformatconversion)
6. Initial candidates are input `DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709` and output `DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020`. These are test settings, not proof of output semantics.
7. Fix source/destination rectangles to original dimensions. Explicitly set automatic enhancement and ordinary filter state; disable spatial upscaling/VSR in the initial experiment.
8. Define the extension payload independently as three 32-bit values: version, method, flags. Statically check its 12-byte size. Enable is flags bit 0; other bits remain 0. Record the observed ABI and actual transmitted bytes.
9. Set the extension on stream index 0 and retain each HRESULT. This API is a general entry point for driver extension data. [Microsoft extension API](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11videocontext-videoprocessorsetstreamextension)
10. Call `VideoProcessorBlt` with valid input/output views and an enabled stream. Read staging data after GPU completion; `Flush` alone does not establish completion.
11. Respect `RowPitch`, dumping only width×4 bytes per row. Run ON/OFF with independent processors or fresh processes to reduce retained-state effects.

### Dump format

- `.rgb10a2`: little-endian packed 32-bit pixels without row padding.
- Interpret bits 0–9 as R, 10–19 as G, 20–29 as B, and 30–31 as A; verify using primary-color patterns.
- `.json`: schema version, dimensions, storage format, pitch policy, requested color space, verified color interpretation or unknown, frame index, PTS, input/raw hashes, extension payload/HRESULT, and settings/environment.
- Do not present nit statistics as established facts when color interpretation is unverified. Label analysis under a PQ assumption as a separate candidate interpretation.

## 7. Encoding and color conversion

The first stable path is planned around converting verified PQ RGB to P010 before encoding. Once BT.2020/PQ RGB is confirmed, do not reapply PQ or repeat BT.709→BT.2020 gamut conversion. Required steps are the BT.2020 NCL Y′CbCr matrix on nonlinear RGB, limited-range quantization, and defined 4:2:0 chroma filtering/siting. P010 stores 10-bit values in the high bits of 16-bit words with the low six bits zero. Compare the GPU implementation against CPU reference calculations.

Direct RGB input is a separate optimization branch. The public header's `ABGR10` places R in the least significant 10 bits, making **ABGR10** the first candidate to compare with R10G10B10A2. Do not connect ARGB10 based on the name alone. Use the SDK header actually selected for the implementation. [NVIDIA format definitions](https://raw.githubusercontent.com/NVIDIA/video-sdk-samples/master/Samples/NvCodec/NvEncoder/nvEncodeAPI.h)

Query NVENC input formats and Main10 capability at runtime. Manage registration, mapping, completion, and release of external D3D11 resources. Internal matrix/range results for direct RGB encoding must pass re-decoding checks; VUI tags alone do not prove correct pixel conversion. NVIDIA documents RGB encoding among features that internally use CUDA, so describe the goal as eliminating application-side CPU frame round trips. [NVENC programming guide](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/nvenc-video-encoder-api-prog-guide/index.html)

Write only known HDR static metadata. Do not copy display peak luminance into MaxCLL. If calculating MaxCLL/MaxFALL, record the luminance method and full-frame analysis coverage. Do not invent missing mastering-display information; validate its omission and player compatibility.

## 8. Timing, resources, and error policy

- Preserve decoder output order and PTS; handle DTS separately for encoder reordering. Use rational timebases.
- Drain decoder/encoder on shutdown, write remaining packets, and finish the container trailer.
- Start with a single submission thread. Later pipelines use bounded queues and explicit completion conditions.
- Stop on resolution changes, device removal, or map/encode failure, recording the frame and cause.
- Rename temporary output to its final name only after success. Permit overwriting existing files only through an explicit option.
- Do not label uncertain effect application as `verified`. A diagnostic command can succeed while HDR validation remains `inconclusive`.

Proposed exit codes: 0=command completed, 2=input/option error, 3=unsupported environment/capability, 4=GPU/API failure, 5=validation failed or inconclusive, 6=encoding/mux failure. Record detailed status in JSON.

## 9. Dependencies and distribution preparation

v0.1 centers on the Windows SDK. Add FFmpeg and Video Codec SDK when needed, pinning versions, download sources, and build options. Neither an impression that the SDK is old nor the source document's claims about the latest version should determine the architecture.

Copying MPC code is outside this plan. Attribute the extension convention used for an independent implementation. Before distribution, review licenses and redistribution conditions for the code, headers, and specific FFmpeg build actually included. Independent authorship alone does not complete every license review.
