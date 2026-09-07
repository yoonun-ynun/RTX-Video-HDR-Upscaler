# Initial implementation results

[English README](../README.md) | [한국어](ko/stage1-results.md)

## Status

Run date: 2026-09-05. NVEncC integration was excluded at the user's request. The final goal is a standalone converter that takes one video and saves an HDR file at the source resolution.

**The C++ build and SDR baseline frame processing/output validation were completed. The HDR extension, actual video input, and NVENC encoding were not implemented at this stage.**

## Actual environment

| Item | Observed value |
|---|---|
| GPU | NVIDIA GeForce RTX 5080, RTX 4060 |
| Test adapter | DXGI index 0, RTX 5080, LUID low 79784 / high 0 |
| Driver | 616.56 |
| Windows build | 26200.8655 |
| Visual Studio | Community 2022 17.13.4 |
| MSVC | 19.43.34809.0 |
| Windows SDK | 10.0.22621.0 |
| CMake | Visual Studio bundled 3.30.5 |

DXGI enumeration listed the RTX 5080 name at indices 0 and 2. The number of names was not treated as the physical GPU count; only index 0 was used. NVIDIA App settings and current monitor HDR state were not investigated in this stage.

The first CMake run failed because duplicate `Path`/`PATH` entries in the child-process environment prevented MSBuild from launching the compiler. Normalizing child-environment keys in the build script fixed it. System environment settings and the Visual Studio installation were unchanged.

## Implemented features

- `probe`: enumerate/select adapters, query D3D11 video interfaces and format/color-space support, and create processor/input/output views where possible.
- `frame`: generate NV12 gray-ramp/color-bars patterns, upload to the GPU, call VideoProcessorBlt, wait on a completion query, and read staging data.
- Little-endian packed RGB10A2 output, JSON sidecars, and CSV of RGB minima/maxima for every frame.
- Save raw data only for the first/last frame to limit disk usage; save once for a one-frame run.
- Refuse to overwrite sources/existing outputs and return nonzero exit codes for invalid arguments or unsupported capabilities.
- PowerShell build and actual GPU validation scripts.

At this stage, `--hdr off` meant that no NVIDIA extension was requested. The next ON/OFF experiment needed a control group explicitly sending enable=0/1 in the extension payload.

## Results

| Test | Result |
|---|---|
| Release x64 build | Succeeded without compiler warnings |
| NV12 → R10G10B10A2 formats | Both input and output supported |
| BT.709 limited → RGB full BT.709 | Conversion supported; resources created |
| BT.709 limited → RGB full BT.2020/PQ | Query HRESULT S_OK; support BOOL false |
| 1920×1080 gray ramp, 120 frames | Passed; first/last raw SHA-256 matched |
| Grayscale ramp | Monotonic; maximum channel difference one code value |
| Black/white medians | Black (0,0,0), white (1023,1023,1022) |
| Eight-color bars | Expected channel order and full range confirmed |
| 1918×1080 row-padding test | GPU RowPitch 7680, saved row 7672 bytes; first/middle/last rows correct |
| Invalid dimensions, HDR ON request, existing output path | Refused with exit code 2 |
| Missing NVIDIA adapter | Refused with exit code 3 |

A 1920×1080 raw file contains 8,294,400 bytes. Color-bar center values were red (1023,2,0), green (0,1023,3), and blue (3,0,1023). NV12 integer quantization and driver conversion rounding explain small deviations from ideal 0/1023.

Detailed outputs are in `artifacts/stage1-validation/verification.json` and neighboring raw/JSON/CSV files. The separate PQ support query is `artifacts/probe-pq.json`. Binaries and large outputs are excluded from Git.

## Gate decisions and next work

- **G0: partial pass.** Required devices, views, and SDR conversion are supported. The initial PQ combination is unsupported by the general capability query.
- **G1: SDR baseline passed.** Upload, channels, range, synchronization, readback, and pitch handling were verified on the actual GPU.
- **G2/G3: not run.** HDR effects and color interpretation were not yet validated.

Next was the `nvidia_hdr_extension` module and explicit enable=0/1 comparison. First observe extension calls and pixel changes using the supported baseline combination. For PQ, investigate whether the general query reflects the vendor-extension path; if necessary, design a separate experimental mode that preserves the unsupported query result. No automatic bypass ignoring that result was implemented.

Then record and compare App settings, Windows HDR, and offscreen dependencies. Extend to video decoding and encoding after validating the color contract.

## Reproduction

In PowerShell 7:

```powershell
./tools/build.ps1
./tools/verify-baseline.ps1
```

A new run directory is created automatically. The script tests adapter 0; if GPU configuration changes, check and update the adapter. Raw input, HDR ON, and convert commands unsupported in this stage were future specifications, not executable features at that time.
