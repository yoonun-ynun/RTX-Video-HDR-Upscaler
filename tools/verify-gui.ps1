param([string]$InputVideo, [string]$BuildDirectory, [ValidateSet('ko','en')][string]$Language = 'ko', [switch]$SkipResume)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (!$BuildDirectory) { $BuildDirectory = Join-Path $repo 'build' }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
if (!$InputVideo) { $InputVideo = Join-Path $repo 'artifacts\first-test-sdr-tagged.mp4' }
$InputVideo = (Get-Item -LiteralPath $InputVideo).FullName
$run = Join-Path $repo ('artifacts\gui-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
& $compiler /nologo /target:winexe /platform:x64 /codepage:65001 /r:System.Windows.Forms.dll /r:System.Drawing.dll /main:GuiSmokeTest /win32manifest:"$repo\src\gui.manifest" /out:"$BuildDirectory\Release\GuiSmokeTest.exe" "$repo\src\gui.cs" "$repo\tests\gui-smoke.cs"
if ($LASTEXITCODE -ne 0) { throw 'GUI test compilation failed' }
Copy-Item -LiteralPath "$repo\src\gui.config" -Destination "$BuildDirectory\Release\GuiSmokeTest.exe.config"
foreach ($case in @(@('mkv','vbr'), @('mp4','cq'), @('mkv','cancel'), @('mkv','fast'), @('mkv','fast-cancel'))) {
    $name = $case[1]
    $output = Join-Path $run "$name.$($case[0])"
    $screenshot = Join-Path $run "$name.png"
    # Quotes are Windows filename-safe; no shell is passed to the GUI or engine.
    $arguments = '"' + $InputVideo + '" "' + $output + '" "' + $screenshot + '" ' + $name + ' ' + $Language
    $process = Start-Process -FilePath "$BuildDirectory\Release\GuiSmokeTest.exe" -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    Get-Content -LiteralPath "$screenshot.txt"
    if ($process.ExitCode -ne 0) { throw "GUI test failed: $name" }
}
& $compiler /nologo /target:winexe /platform:x64 /codepage:65001 /r:System.Windows.Forms.dll /r:System.Drawing.dll /main:GuiStagesTest /win32manifest:"$repo\src\gui.manifest" /out:"$BuildDirectory\Release\GuiStagesTest.exe" "$repo\src\gui.cs" "$repo\tests\gui-stages.cs"
if ($LASTEXITCODE -ne 0) { throw 'GUI stage test compilation failed' }
Copy-Item -LiteralPath "$repo\src\gui.config" -Destination "$BuildDirectory\Release\GuiStagesTest.exe.config"
$stageImage = Join-Path $run 'mux-stage.png'
$stageProcess = Start-Process -FilePath "$BuildDirectory\Release\GuiStagesTest.exe" -ArgumentList ('"' + $stageImage + '"') -WindowStyle Hidden -Wait -PassThru
Get-Content -LiteralPath "$stageImage.txt"
if ($stageProcess.ExitCode -ne 0) { throw 'GUI stage test failed' }
Write-Output "GUI tests passed: $run"

& $compiler /nologo /target:winexe /platform:x64 /codepage:65001 /r:System.Windows.Forms.dll /r:System.Drawing.dll /main:GuiResumeTest /win32manifest:"$repo\src\gui.manifest" /out:"$BuildDirectory\Release\GuiResumeTest.exe" "$repo\src\gui.cs" "$repo\tests\gui-resume.cs"
if ($LASTEXITCODE -ne 0) { throw 'GUI resume test compilation failed' }
Copy-Item -LiteralPath "$repo\src\gui.config" -Destination "$BuildDirectory\Release\GuiResumeTest.exe.config"
$resumeInput = Join-Path $repo 'artifacts\native-long-tagged.mp4'
if (!$SkipResume -and (Test-Path -LiteralPath $resumeInput)) {
    $resumeOutput = Join-Path $run 'resume.mkv'
    $resumeArgs = '"' + $resumeInput + '" "' + $resumeOutput + '"'
    $resumeProcess = Start-Process -FilePath "$BuildDirectory\Release\GuiResumeTest.exe" -ArgumentList $resumeArgs -WindowStyle Hidden -Wait -PassThru
    Get-Content -LiteralPath "$resumeOutput.test.txt" -Tail 8
    if ($resumeProcess.ExitCode -ne 0) { throw 'GUI cancel/reopen/resume test failed' }
}
