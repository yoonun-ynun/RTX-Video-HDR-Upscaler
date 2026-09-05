param([string]$InputVideo)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (!$InputVideo) { $InputVideo = Join-Path $repo 'artifacts\first-test-sdr-tagged.mp4' }
$InputVideo = (Get-Item -LiteralPath $InputVideo).FullName
$run = Join-Path $repo ('artifacts\gui-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
& $compiler /nologo /target:winexe /platform:x64 /codepage:65001 /r:System.Windows.Forms.dll /r:System.Drawing.dll /main:GuiSmokeTest /win32manifest:"$repo\src\gui.manifest" /out:"$repo\build\Release\GuiSmokeTest.exe" "$repo\src\gui.cs" "$repo\tests\gui-smoke.cs"
if ($LASTEXITCODE -ne 0) { throw 'GUI test compilation failed' }
Copy-Item -LiteralPath "$repo\src\gui.config" -Destination "$repo\build\Release\GuiSmokeTest.exe.config"
foreach ($case in @(@('mkv','vbr'), @('mp4','cq'), @('mkv','cancel'))) {
    $name = $case[1]
    $output = Join-Path $run "$name.$($case[0])"
    $screenshot = Join-Path $run "$name.png"
    # Quotes are Windows filename-safe; no shell is passed to the GUI or engine.
    $arguments = '"' + $InputVideo + '" "' + $output + '" "' + $screenshot + '" ' + $name
    $process = Start-Process -FilePath "$repo\build\Release\GuiSmokeTest.exe" -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    Get-Content -LiteralPath "$screenshot.txt"
    if ($process.ExitCode -ne 0) { throw "GUI test failed: $name" }
}
Write-Output "GUI tests passed: $run"
