# Additional guidance for v0.4.0

[English README](../README.md) | [한국어](ko/gui-v0.3.md)

On first launch, use **Install required components** or `Setup-Runtime.cmd` to install FFmpeg shared DLLs and tools. The installer uses a date-pinned official BtbN release and SHA256 verification, then reopens the GUI. The default converter uses the direct GPU path. Video selection, saved quality settings, GPU selection, MKV/MP4 output, and cancellation work as before.

The guide below continues the documentation from earlier versions. v0.4.2 supports both English and Korean; choose the language in the GUI.

# RTX Video HDR Upscaler GUI v0.3.5

Run `RTXVideoHDR.exe`. The neighboring `RTXVideoHDRConvert.exe` performs conversion, so keep both executables and the `.config` file together. The application uses Windows .NET Framework 4.8 and requires no separate server or browser.

1. Choose a source video or drop one file onto the window. Changing the source automatically updates the output to an HDR filename beside the new source. Manually chosen output paths are reset; the selected MKV/MP4 format is preserved.
2. Choose an output path, NVIDIA GPU, and MKV/MP4 format.
3. Select quality-based CQ (default 18) or average bitrate VBR (Mbps).
4. Click **Start HDR upscaling**. Frame count, average speed, estimated time remaining, and logs are displayed.
5. After completion, use **Play result** or **Output folder**.

MKV and MP4 are containers. Both store HEVC Main10 / BT.2020 / PQ HDR video. MKV copies the original audio; MP4 re-encodes to AAC with a 320 kbps target for compatibility and uses hvc1 and faststart. Resolution and frame rate are preserved. Subtitles and chapters are not copied.

The GPU list comes from the engine's DXGI enumeration. The selected adapter is passed to the decoder, HDR processor, and encoder. The encoder uploads P010 to a D3D11 device created on that same DXGI adapter and uses NVENC, avoiding incorrect GPU selection when CUDA and DXGI indices differ. This v0.3 path still copies frames between processes through CPU memory.

The entire input is not pre-decoded; PTS is checked during conversion. **Preview conversion** processes only the first 432 frames. Use the BT.709 assumption only for known SDR video with missing color tags. Existing output is not overwritten. Cancellation or closing the window during a job stops its converter process; closing the engine's job object also cleans up its FFmpeg children. Failed/canceled jobs retain temporary files in `rtxhdr-run-*`.

## Verified items

- RTX 5080/4060 appeared in the GPU list.
- GUI conversion on RTX 4060 to MP4 completed for a 1080p 240-frame input; progress events were received.
- Re-decoding that MP4 produced 240 frames. HEVC Main10/hvc1/BT.2020/PQ, AAC, and eight-second playback duration were confirmed.
- Canceling during GUI conversion left no final output file.
- The layout was rendered at 150% DPI to check text, fields, and buttons for clipping.
- Reproduce GUI MKV/VBR, MP4/CQ, and cancellation tests with `tools/verify-gui.ps1`.
- The latest engine's MKV path is covered by GPU color sample checks and independent output validation.

Use `tools/build.ps1` for the build or `tools/build-gui.ps1` for the GUI alone. The CLI remains available.

```powershell
RTXVideoHDRConvert.exe --list-gpus
RTXVideoHDRConvert.exe "input.mp4" --adapter 1 --output "output.mp4" --bitrate 40M
```

See the [v0.2 record](performance-v0.2.md) for earlier performance measurements. Those rates are not guaranteed for v0.3's new GPU-specific encoding path. Completion of the entire 23-minute video and every GPU/driver combination remained unverified at this stage.

## v0.3.1 GUI changes

Fixed output-path updates when the source changes. The title and description now identify NVIDIA driver RTX Video HDR processing, HEVC 10-bit output, and preservation of resolution/frame rate. MKV/MP4 path updates, manual-path reset, clearing the output when the input is removed, and layout at 150% DPI were checked. The conversion engine remained v0.3.

## v0.3.2 Wording changes

Changed the title to “RTX Video HDR Upscaler” and explicitly mentioned HDR upscaling in the description and start button. Here, upscaling means SDR-to-HDR processing; resolution and frame rate are preserved.

## v0.3.3 Settings file

`settings.ini` beside the executable automatically saves CQ/VBR mode, CQ, bitrate in Mbps, MKV/MP4 format, and GPU index/name, then restores them on launch. Preview and assumed-color checkboxes reset each time. Source/output paths are not saved.

Settings are saved on changes and exit using a temporary file followed by replacement. If the saved GPU is missing, the first available GPU is used. Invalid or out-of-range values fall back to defaults. Permission failures appear in the GUI log.

To reset, close the application and delete `settings.ini`. Copy the existing settings file when moving to a new distribution folder. A separate process termination/relaunch test verified VBR/CQ values, MP4, and GPU restoration, as well as invalid-value and missing-GPU fallbacks. Reproduce with `tools/verify-gui-settings.ps1`.

## v0.3.4 FPS display

Recent five-second throughput and cumulative average are shown separately; the remaining-time estimate uses recent speed. During the first five seconds, the actual elapsed interval is used. A monotonic clock measures frames delivered to the encoder, not GPU-only execution. Final encoder shutdown, audio processing, and file finalization are excluded. Updates occur about every 0.5 seconds as frames complete, so a long wait also temporarily pauses the display.

Deterministic time/frame sequences verified a stable average at constant speed, separation of recent speed and cumulative average after a fast start, stalls, and slow startup. This changes live reporting; it is not a throughput optimization.

## v0.3.5 Video completion and audio muxing status

When stdout was connected to the GUI pipe, a newline did not always deliver text immediately. Stage messages now explicitly flush, and separate events report encoder finalization, audio copy/AAC muxing, output validation, and final saving.

After the last frame, the GUI displays encoder finalization and 100% frame progress. Muxing switches to an activity indicator. Late FPS messages cannot revert the display to video processing. Overall completion appears only after successful engine exit and confirmation of the final file.

## Resume conversion (v0.4.1)

After failure or cancellation, click **Resume conversion** and select the job folder's `checkpoint.txt`. Quality, GPU, and output settings are restored. The default GPU path saves roughly every 10 seconds of source video and reprocesses only the unfinished segment. If video processing is complete, it resumes at audio muxing or final saving. The most recent job location persists across launches. Keep the source and entire job folder, and do not change the build/DLLs or NVIDIA App effect settings before completion. See the README's resume section for detailed limits and diagnostic files.

The **Save segments for new conversions (resume support)** checkbox lets you choose between checkpoint overhead and resume support. It is enabled by default. Disabling it prioritizes speed and makes that new job non-resumable. The selection is saved in the `checkpoint` setting in `settings.ini`. It does not affect **Resume conversion** for existing jobs.

## Automatic cleanup after completion (v0.4.2)

After successful audio muxing, validation, and final saving, **Cleaning intermediate files** appears while large temporary files, including segment videos, are deleted. Logs and checkpoint metadata remain. Failed/canceled jobs preserve resume files. If some files cannot be removed, the GUI reports conversion success with a cleanup notice and records details in `cleanup.json`. Reopening a completed checkpoint job verifies the final output and retries cleanup of remaining files.

## Interface language (v0.4.2)

Select **English** or **한국어** from the **Language / 언어** dropdown at the top right. The interface updates immediately, even while converting, without resetting paths, quality settings, or job progress. The choice is restored from `[interface] language=en/ko` in `settings.ini` on the next launch. Missing/invalid values use Korean on Korean Windows and English elsewhere. Old settings files remain supported.

Application labels, stage messages, input errors, and file-picker titles are localized. Existing log history and engine/FFmpeg diagnostics stay as recorded; Windows-owned dialog buttons use Windows settings.

Validation covers both languages, switching during progress and mux/cleanup stages, preserving paths/settings, translated input errors, fresh-process language restoration, default/legacy settings, and layout at 150% DPI. Run `tools/verify-gui-language.ps1 -BuildDirectory <build-folder>`. Actual conversion tests support `tools/verify-gui.ps1 -BuildDirectory <build-folder> -Language en` or `-Language ko`.

## SDR/HDR split comparison (v0.4.3)

Select **Compare SDR / HDR: left SDR · right HDR** to use an SDR reference on the left half of each frame and RTX HDR on the right. The original composition is preserved; two complete images are not squeezed side by side. Leave **Preview: first 432 frames** off for the full video, or enable it to stop after 432 frames. Shorter inputs stop at EOF. Comparison defaults to off on launch; resuming a checkpoint restores the saved comparison mode.

The automatic filename is `source.compare.hdr.mkv` or `.mp4`. Toggling comparison preserves a manually chosen path. Choosing a new source resets the path beside that source with the comparison suffix. GPU selection, CQ/VBR, container/audio muxing, optional checkpoints and successful intermediate cleanup work as in normal conversion.

The whole output is BT.2020/PQ. A separate GPU Video Processor keeps NVIDIA HDR disabled for the left reference; its SDR RGB is linearized, converted from BT.709 to BT.2020 primaries, then PQ encoded with white at 203 nits. The right HDR RGB is not transformed again. The SDR output format follows DXGI `RGB_FULL_G22_NONE_P709`'s [piecewise sRGB transfer definition](https://learn.microsoft.com/en-us/windows/win32/api/dxgicommon/ne-dxgicommon-dxgi_color_space_type). The 203-nit white is this application's fixed reference, not a reproduction of the Windows SDR brightness setting. Use an HDR display and player for comparison.

Both Video Processors use the same decoded frame and D3D11 device. Composition runs in the existing P010 GPU shader, adding no CPU frame round trip to the default GPU path. SDR reference processing still adds GPU work and VRAM use. The split is aligned to an even pixel to avoid mixing SDR and HDR in a 4:2:0 chroma block. For widths not divisible by four, the left side ends one pixel before the midpoint.

Use `--compare-sdr-hdr` from the CLI; add `--max-frames 432` to limit duration and `--no-checkpoint` to disable saved segments. `--pipe-video` is supported. Raw RGB diagnostics (`--diagnostics`, `--cpu-color`) are incompatible with comparison. Checkpoint v2 stores comparison mode. Jobs created by an older build still require their original converter build to resume.

`GpuComparisonTests.exe <GPU index>` checks 1918/1920 widths, 8/10-bit input, ramps/color bars, SDR reference correctness, HDR preservation and native-texture/pipe-buffer agreement. Default CTest includes checkpoint v2 round trips and legacy defaults. `tools/verify-gui.ps1` covers normal/full/preview comparison, MKV/MP4, cancellation and resume.
