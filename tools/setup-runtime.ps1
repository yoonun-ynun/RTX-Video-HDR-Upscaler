param([string]$Destination = (Split-Path -Parent $PSScriptRoot), [string]$Archive)
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$Destination = [IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$lock = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'ffmpeg-native-lock.json') -Raw | ConvertFrom-Json
$stage = Join-Path $Destination ('.runtime-setup-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
try {
    if (!$Archive) {
        $Archive = Join-Path $stage 'runtime.zip'
        Write-Host 'Downloading verified FFmpeg runtime from BtbN / GitHub...'
        Invoke-WebRequest -UseBasicParsing -Uri $lock.url -OutFile $Archive
    }
    $Archive = (Get-Item -LiteralPath $Archive).FullName
    if ((Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash -ne $lock.sha256) { throw 'Runtime archive SHA256 mismatch. Nothing was installed.' }
    $expanded = Join-Path $stage 'expanded'
    Expand-Archive -LiteralPath $Archive -DestinationPath $expanded
    $roots = @(Get-ChildItem -LiteralPath $expanded -Directory)
    if ($roots.Count -ne 1) { throw 'Unexpected runtime archive structure.' }
    $bin = Join-Path $roots[0].FullName 'bin'
    $required = @('avcodec-62.dll','avformat-62.dll','avutil-60.dll','swresample-6.dll','ffmpeg.exe','ffprobe.exe')
    foreach ($name in $required) { if (!(Test-Path -LiteralPath (Join-Path $bin $name) -PathType Leaf)) {throw "Missing runtime file: $name"} }
    Get-ChildItem -LiteralPath $bin -File -Filter '*.dll' | Copy-Item -Destination $Destination -Force
    foreach ($name in @('ffmpeg.exe','ffprobe.exe')) {
        if ((Test-Path -LiteralPath (Join-Path $Destination $name)) -or (Get-Command $name -CommandType Application -ErrorAction SilentlyContinue)) {
            Write-Host "Reusing existing $name"
        } else {Copy-Item -LiteralPath (Join-Path $bin $name) -Destination $Destination}
    }
    Copy-Item -LiteralPath (Join-Path $roots[0].FullName 'LICENSE.txt') -Destination (Join-Path $Destination 'FFmpeg-LICENSE.txt') -Force
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'ffmpeg-native-lock.json') -Destination (Join-Path $Destination 'runtime-version.json') -Force
    Write-Host 'FFmpeg runtime installed. Local settings were preserved.'
} finally {
    $resolved = [IO.Path]::GetFullPath($stage)
    if ($resolved.StartsWith($Destination.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetFileName($resolved).StartsWith('.runtime-setup-')) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
