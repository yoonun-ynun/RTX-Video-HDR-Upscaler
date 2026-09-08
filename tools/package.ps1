param([string]$Version = '0.4.4', [string]$OutputDirectory, [string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+(\.\d+)?$') { throw 'Invalid release version' }
$repo = Split-Path -Parent $PSScriptRoot
if (!$BuildDirectory) { $BuildDirectory = Join-Path $repo build }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$cache = Get-Content -LiteralPath (Join-Path $BuildDirectory 'CMakeCache.txt') -Raw
if ($cache -notmatch '(?m)^FFMPEG_ROOT:PATH=.+') { throw 'Release packaging requires a native FFmpeg build.' }
$help = & (Join-Path $BuildDirectory 'Release/RTXVideoHDRConvert.exe') --help
if ($LASTEXITCODE -ne 0 -or ($help -join "`n") -notmatch ('(?m)^RTX Video HDR Convert ' + [regex]::Escape("v$Version") + '\r?$')) { throw 'Built engine version does not match package version.' }
$destination = if ($OutputDirectory) { [System.IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $repo "release\v$Version" }
if (Test-Path -LiteralPath $destination) { throw 'Release directory already exists; choose a new version.' }
New-Item -ItemType Directory -Path $destination | Out-Null
Copy-Item -LiteralPath "$BuildDirectory\Release\RTXVideoHDRConvert.exe" -Destination "$destination\RTXVideoHDRConvert.exe"
Copy-Item -LiteralPath "$BuildDirectory\Release\RTXVideoHDR.exe" -Destination "$destination\RTXVideoHDR.exe"
Copy-Item -LiteralPath "$BuildDirectory\Release\RTXVideoHDR.exe.config" -Destination "$destination\RTXVideoHDR.exe.config"
Copy-Item -LiteralPath "$repo\src\default-settings.ini" -Destination "$destination\settings.ini"
Copy-Item -LiteralPath "$repo\README.md" -Destination "$destination\README.md"
Copy-Item -LiteralPath "$repo\docs" -Destination "$destination\docs" -Recurse
New-Item -ItemType Directory -Path "$destination\tools" | Out-Null
Copy-Item -LiteralPath "$repo\tools\setup-runtime.ps1","$repo\tools\ffmpeg-native-lock.json" -Destination "$destination\tools"
Copy-Item -LiteralPath "$repo\tools\Setup-Runtime.cmd" -Destination "$destination\Setup-Runtime.cmd"
Set-Content -LiteralPath "$destination\native-runtime.required" -Value 'FFmpeg shared runtime required; use Setup-Runtime.cmd' -Encoding ascii
$manifest = Get-ChildItem -LiteralPath $destination -File -Recurse | ForEach-Object {
    @{ file=[System.IO.Path]::GetRelativePath($destination,$_.FullName).Replace('\','/'); bytes=$_.Length; sha256=(Get-FileHash -LiteralPath $_.FullName).Hash }
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath "$destination\manifest.json" -Encoding utf8
Write-Output "Packaged: $destination"
