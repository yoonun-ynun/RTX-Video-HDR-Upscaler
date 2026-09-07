# Validation procedures and acceptance criteria

[English README](../README.md) | [한국어](ko/validation-plan.md)

## 1. Core questions

1. In a supported environment, does extension ON produce a reproducible difference from OFF?
2. Can the result be read from a texture without presentation to a screen?
3. Which color space should be used to interpret output code values?
4. Does saving and re-decoding preserve the processed result?
5. How closely does it match browser output under the same conditions?

The criteria below are this project's proposed initial rules, not NVIDIA-guaranteed accuracy requirements. Do not loosen thresholds after the fact merely to remove failures; document the reason for any change.

## 2. Run records

Save each run in a separate directory:

```text
artifacts/<run-id>/
  environment.json
  settings.json
  calls.jsonl
  input.nv12
  frames/off-*.rgb10a2
  frames/on-*.rgb10a2
  frames/*.json
  metrics.csv
  report.md
```

Record GPU name/LUID, driver, Windows build, NVIDIA App version/settings, HDR state, monitor/connection layout, resolution/refresh rate, application build ID, command, and input hash. Record unavailable automatic queries manually or as unknown. Start App-setting experiments by recording settings changed by the user before attempting automation.

## 3. Test data

- BT.709 limited NV12 grayscale steps and continuous ramps, explicitly identifying Y 16/235 boundaries.
- Neutral gray, primary, and complementary-color patches to detect channel swaps and range errors.
- Small/large white windows, low/high APL scenes, and gradients.
- SDR clips with appropriate usage rights, including skin tones, daylight, dark scenes/light sources, and rapid scene changes.

Patterns should fill at least a 1080p frame. Do not judge content-adaptive behavior from a single uniformly colored pixel. Repeat static patterns for 120 frames and analyze the first 60 separately from later frames. Sixty frames is an initial warmup assumption, not a stabilization guarantee. If variation persists, report a longer sequence and its temporal behavior.

## 4. Core experiment matrix

| Experiment | Fixed conditions | Varied conditions | Purpose |
|---|---|---|---|
| E01 | Same input, output color space, and environment | Extension OFF/ON | Detect a candidate effect |
| E02 | Extension ON, same input | One App setting at a time | Determine whether global settings affect output |
| E03 | Same settings and input | Three fresh processes | Check reproducibility and retained state |
| E04 | Same input, extension ON | Windows HDR ON/OFF | Identify display-environment dependency |
| E05 | Same input, extension ON | Offscreen/presented path | Investigate offscreen constraints |
| E06 | Same content and settings | Repeated frames/actual sequence | Investigate temporal behavior |
| E07 | Same HDR result | P010/direct RGB encoding | Check the output path |
| E08 | Same video and display conditions | Chrome/direct call/SDK | Compare appearance and luminance |

Do not run every combination at once. Start with E01/E03, expanding to E04/E05 if there is no effect. Record an unsupported SDR→PQ combination as a failure and investigate other combinations under separate experiment IDs. Restart the application by default when changing settings; immediate application of changes is a separate observation.

## 5. Comparison methods

### Extension ON/OFF

Both groups must use the same output color space. Otherwise, ordinary color-conversion differences can be mistaken for an HDR-extension effect.

- Calculate frame hashes, RGB code-value MAE, p95/p99, patch medians, clipping ratios, and changes over time.
- Measure within-condition variation across repeated runs first.
- Initial effect criterion: in preselected bright areas, the ON/OFF difference must exceed the larger of two 10-bit code values or five times repeat-run variation, with the same trend in three runs.
- This detects an effect candidate. Actual HDR meaning requires the color checks below. If differences are small, classify the result as inconclusive and test other content/environments.

### Color interpretation

- First check channel order, full/limited range, and black/white positions.
- Calculate patch luminance and waveforms under a candidate PQ interpretation, explicitly recording that PQ is an assumption.
- Check consistency between the requested color space, numerical distribution, and controlled HDR presentation. Do not claim a single ramp mathematically proves the internal transfer function.
- Calibrate the analyzer and display path with separate PQ test frames using known code values. Do not assume the HDR algorithm must map SDR white to a particular nit level.
- If color semantics remain insufficiently established, save raw research results only and defer product HDR encoding.

### Browser/SDK comparison

Ordinary PNG screenshots may contain OS/browser tone mapping. First verify whether the capture method preserves HDR/scRGB and establish its transfer, primaries, and scale. Use captures with ambiguous interpretation only for qualitative comparison.

Capture the same video frame at the same size, on the same monitor, HDR state, and settings, excluding UI and letterboxing. Normalize to the same color representation before comparing code values. Record SDR white level, ICC/color management, VSR, browser version, and SDK parameters.

Proposed initial similarity targets are median patch luminance error within 5% and p95 within 10%. Use a separate absolute-error criterion of 0.1 nit for patches below 1 nit. Do not declare a pass if measurement uncertainty exceeds these limits. Report skin/color patches and highlight clipping separately.

Even if `Chrome ≈ direct call` and `Chrome ≠ SDK` are observed, the conclusion is limited to output differences under those conditions/settings. Do not extend that observation into a claim about different AI models or internal algorithms.

## 6. Stage gates

| Gate | Acceptance condition | Action on failure/inconclusive result |
|---|---|---|
| G0 Environment | Hardware D3D11 device and required interfaces, views, and format combinations confirmed | Report environment and unsupported capability |
| G1 Raw pipeline | OFF dump size=width×height×4; channels and row handling verified | Fix upload/readback |
| G2 Effect | E01/E03 differences exceed repeat-run variation and show candidate HDR characteristics | Investigate E04/E05, warmup, and content conditions |
| G3 Color contract | Raw meaning, presentation, and analysis agree; interpretation evidence documented | Defer encoding and compare candidate interpretations |
| G4 Storage | Main10/10-bit and color tags confirmed; re-decoded patches/range validated | Fix color conversion or encoder path |
| G5 Video | 10-second → 1-minute → 10-minute tests; no missing/duplicate frames; A/V end difference within one frame | Fix PTS/DTS, draining, and lifetime management |
| G6 Optimization | Quality preserved against baseline; performance, CPU round trips, and memory measured | Keep the verified baseline path |

The initial RGB→P010 CPU/GPU numerical tolerance is one code value per channel with identical rounding/filtering. Do not apply that limit to every pixel after lossy compression. For high-quality encoding tests, the initial target is re-decoded Y′CbCr median error within three code values on flat patches, with edges reported separately.

E08 browser comparison separately evaluates the product's reproduction target. If capture calibration is blocked, other file-output validation can continue, but browser-equivalence claims remain deferred.

## 7. Failure branches

- Failed HRESULT: first check GUID/payload, required interfaces, format combinations, and GPU selection.
- Successful HRESULT but identical output: check App toggle, Windows HDR, input conditions, repeated frames, and presentation path in that order.
- Offscreen-only failure: add a small HDR presentation diagnostic path. Separately verify whether its processed texture can be read back.
- Persistent presentation dependency: decide in the experiment report whether to proceed as a constrained tool. Do not claim fully unattended conversion.
- Gray/dark output: investigate range, channel order, duplicate PQ, primaries, and capture tone mapping in that order.
- Incorrect direct RGB encoding only: use explicit P010 conversion.
- Regression after a driver change: rerun the same fixtures and maintain a list of verified versions.

If G2 remains inconclusive after all experiment branches, collect a failure report and raw data and stop expanding that path. Do not present unverified output as successful HDR conversion.
