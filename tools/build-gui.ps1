$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$destination = Join-Path $repo 'build\Release'
New-Item -ItemType Directory -Force -Path $destination | Out-Null
& $compiler /nologo /target:winexe /platform:x64 /optimize+ /codepage:65001 /r:System.Windows.Forms.dll /r:System.Drawing.dll /win32manifest:"$repo\src\gui.manifest" /out:"$destination\RTXVideoHDR.exe" "$repo\src\gui.cs"
if ($LASTEXITCODE -ne 0) { throw 'GUI compilation failed' }
Copy-Item -LiteralPath "$repo\src\gui.config" -Destination "$destination\RTXVideoHDR.exe.config"
if (!(Test-Path -LiteralPath "$destination\settings.ini")) {
    Copy-Item -LiteralPath "$repo\src\default-settings.ini" -Destination "$destination\settings.ini"
}
Write-Output "GUI built: $destination\RTXVideoHDR.exe"
