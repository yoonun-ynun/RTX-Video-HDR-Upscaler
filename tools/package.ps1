param([string]$Version = '0.3.4', [string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+(\.\d+)?$') { throw 'Invalid release version' }
$repo = Split-Path -Parent $PSScriptRoot
$destination = if ($OutputDirectory) { [System.IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $repo "release\v$Version" }
if (Test-Path -LiteralPath $destination) { throw 'Release directory already exists; choose a new version.' }
New-Item -ItemType Directory -Path $destination | Out-Null
Copy-Item -LiteralPath "$repo\build\Release\RTXVideoHDRConvert.exe" -Destination "$destination\RTXVideoHDRConvert.exe"
Copy-Item -LiteralPath "$repo\build\Release\RTXVideoHDR.exe" -Destination "$destination\RTXVideoHDR.exe"
Copy-Item -LiteralPath "$repo\build\Release\RTXVideoHDR.exe.config" -Destination "$destination\RTXVideoHDR.exe.config"
Copy-Item -LiteralPath "$repo\src\default-settings.ini" -Destination "$destination\settings.ini"
Copy-Item -LiteralPath "$repo\docs\performance-v0.2.md" -Destination "$destination\test-results.md"
Copy-Item -LiteralPath "$repo\docs\gui-v0.3.md" -Destination "$destination\사용법.md"
Copy-Item -LiteralPath "$repo\docs\performance-v0.2.md" -Destination "$destination\performance-v0.2.md"
Copy-Item -LiteralPath "$repo\README.md" -Destination "$destination\README.md"
Copy-Item -LiteralPath "$repo\docs" -Destination "$destination\docs" -Recurse
$manifest = Get-ChildItem -LiteralPath $destination -File -Recurse | ForEach-Object {
    @{ file=[System.IO.Path]::GetRelativePath($destination,$_.FullName).Replace('\','/'); bytes=$_.Length; sha256=(Get-FileHash -LiteralPath $_.FullName).Hash }
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath "$destination\manifest.json" -Encoding utf8
Write-Output "Packaged: $destination"
