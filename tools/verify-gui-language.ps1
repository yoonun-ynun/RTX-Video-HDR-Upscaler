param([string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (!$BuildDirectory) { $BuildDirectory = Join-Path $repo build }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$run = Join-Path $repo ('artifacts\language-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
& $compiler /nologo /target:winexe /platform:x64 /codepage:65001 /r:System.Windows.Forms.dll /r:System.Drawing.dll /main:GuiLanguageTest /win32manifest:"$repo\src\gui.manifest" /out:"$BuildDirectory\Release\GuiLanguageTest.exe" "$repo\src\gui.cs" "$repo\tests\gui-language.cs"
if ($LASTEXITCODE -ne 0) { throw 'Language test compilation failed' }
Copy-Item -LiteralPath "$repo\src\gui.config" -Destination "$BuildDirectory\Release\GuiLanguageTest.exe.config"
foreach ($operation in @('defaults','write-en','read-en','write-ko','read-ko','ui')) {
    $arguments = '"' + "$run\settings.ini" + '" ' + $operation
    $process = Start-Process -FilePath "$BuildDirectory\Release\GuiLanguageTest.exe" -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    Get-Content -LiteralPath "$run\settings.ini.$operation.txt"
    if ($process.ExitCode -ne 0) { throw "Language test failed: $operation" }
}
Write-Output "Language tests passed: $run"
