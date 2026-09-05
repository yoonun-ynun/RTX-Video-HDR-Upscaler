$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$run = Join-Path $repo ('artifacts\settings-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
& $compiler /nologo /target:winexe /platform:x64 /codepage:65001 /r:System.Windows.Forms.dll /r:System.Drawing.dll /main:GuiSettingsTest /win32manifest:"$repo\src\gui.manifest" /out:"$repo\build\Release\GuiSettingsTest.exe" "$repo\src\gui.cs" "$repo\tests\gui-settings.cs"
if ($LASTEXITCODE -ne 0) { throw 'Settings test compilation failed' }
Copy-Item -LiteralPath "$repo\src\gui.config" -Destination "$repo\build\Release\GuiSettingsTest.exe.config"
foreach ($operation in @('write','read','invalid')) {
    $arguments = '"' + "$run\settings.ini" + '" ' + $operation
    $process = Start-Process -FilePath "$repo\build\Release\GuiSettingsTest.exe" -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    Get-Content -LiteralPath "$run\settings.ini.$operation.txt"
    if ($process.ExitCode -ne 0) { throw "Settings test failed: $operation" }
}
Write-Output "Settings tests passed: $run"
