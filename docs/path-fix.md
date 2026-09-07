# v0.1.1 — Fix for “Output write failed” at startup

[English README](../README.md) | [한국어](ko/path-fix.md)

In a user run, probing the video succeeded, but writing the first `input-info.txt` failed. The failing path was 261 characters long; `probe.log` in the same folder was 256 characters and `error.json` was 257. The causes were reusing the full source filename in the log directory name and file-writing code that did not handle Windows legacy path-length limits.

Earlier tests used the original source but chose a short output name, missing the defect in the default output path.

Changes:

- Create a log folder named `rtxhdr-run-PROCESS_ID-TIME_VALUE`, independent of the output filename.
- Use Windows extended-length Unicode paths for application file writes, process logs, and final file moves. System settings are not changed.
- Use `CREATE_NEW` to protect existing files atomically.
- Include the failed path and actual Win32 error code in file errors.

Validation:

- Exact data write/read succeeded on 261- and 340-character paths containing Korean text and spaces.
- Existing-file overwrite was refused; failure messages included the path and error code.
- Converted three frames from the same full source file and saved a long output filename: `artifacts/path-bug/rtxhdr-run-72500-286615968/result.json`.
- Converted three frames from an excerpt with the same long filename as the source, using the default output path without `--output`: `artifacts/default-path-bug/rtxhdr-run-56404-286650937/result.json`.
- Both runs passed the application's HEVC Main10, HDR color-tag, and re-decoded frame-count checks. These tests addressed startup failure and path handling; they do not establish successful conversion of the entire 23-minute video.

Regression test after building: `build/Release/FilePathTests.exe` or CTest `file_paths`.
