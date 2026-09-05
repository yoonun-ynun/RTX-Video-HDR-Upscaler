param([string]$RunName = ('verify-' + (Get-Date -Format 'yyyyMMdd-HHmmss')))
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repo 'build\Release\RTXVideoHDRTest.exe'
$runs = Join-Path $repo 'artifacts'
if ($RunName -notmatch '^[a-zA-Z0-9_-]+$') { throw 'RunName must be a simple directory name' }
$out = Join-Path $runs $RunName
if (Test-Path -LiteralPath $out) { throw 'Verification directory already exists' }
New-Item -ItemType Directory -Path $out | Out-Null
function Run-Test([string[]]$Arguments, [int]$Expected = 0) {
    & $exe @Arguments
    if ($LASTEXITCODE -ne $Expected) { throw "Unexpected exit code $LASTEXITCODE; expected $Expected" }
}
function Pixel([byte[]]$Bytes, [int]$Width, [int]$X, [int]$Y) {
    $word = [BitConverter]::ToUInt32($Bytes, 4 * ($Y * $Width + $X))
    return @([int]($word -band 1023), [int](($word -shr 10) -band 1023), [int](($word -shr 20) -band 1023))
}
Run-Test @('probe','--adapter','0','--output-color','sdr','--report',"$out\probe.json")
Run-Test @('frame','--adapter','0','--output-color','sdr','--pattern','gray-ramp','--frames','120','--out',"$out\gray")
Run-Test @('frame','--adapter','0','--output-color','sdr','--pattern','color-bars','--out',"$out\bars")
# Non-aligned row width detects accidentally writing GPU row padding to disk.
Run-Test @('frame','--adapter','0','--size','1918x1080','--output-color','sdr','--out',"$out\pitch")
$gray = [IO.File]::ReadAllBytes("$out\gray\frame-0.rgb10a2")
if ($gray.Length -ne 1920*1080*4) { throw 'Incorrect packed file size' }
$previous = -1
for ($x=0; $x -lt 1920; ++$x) {
    $rgb = Pixel $gray 1920 $x 540
    if (($rgb | Measure-Object -Maximum).Maximum - ($rgb | Measure-Object -Minimum).Minimum -gt 1) { throw 'Non-neutral gray' }
    if ($rgb[0] -lt $previous) { throw 'Non-monotonic gray ramp' }
    $previous = $rgb[0]
}
$black = Pixel $gray 1920 0 540
$white = Pixel $gray 1920 1919 540
if ($black[0] -gt 2 -or $white[0] -lt 1021) { throw 'Incorrect black/white range' }
if ((Get-FileHash "$out\gray\frame-0.rgb10a2").Hash -ne (Get-FileHash "$out\gray\frame-119.rgb10a2").Hash) { throw 'First/last gray frame mismatch' }
$bars = [IO.File]::ReadAllBytes("$out\bars\frame-0.rgb10a2")
$samples = @()
$expected = @(@(1,1,1),@(1,1,0),@(0,1,1),@(0,1,0),@(1,0,1),@(1,0,0),@(0,0,1),@(0,0,0))
for ($bar=0; $bar -lt 8; ++$bar) {
    $rgb = Pixel $bars 1920 (120 + 240*$bar) 540
    for ($c=0; $c -lt 3; ++$c) {
        if ($expected[$bar][$c] -eq 1 -and $rgb[$c] -lt 1000) { throw 'Color channel too low' }
        if ($expected[$bar][$c] -eq 0 -and $rgb[$c] -gt 24) { throw 'Color channel too high' }
    }
    $samples += ,$rgb
}
$pitch = [IO.File]::ReadAllBytes("$out\pitch\frame-0.rgb10a2")
$pitchMeta = Get-Content "$out\pitch\frame-0.json" -Raw | ConvertFrom-Json
if ($pitch.Length -ne 1918*1080*4) { throw 'Row padding leaked into raw file' }
foreach ($row in @(0,1,539,1079)) {
    $rgb = Pixel $pitch 1918 1917 $row
    if ($rgb[0] -lt 1021) { throw 'Row stride handling failed' }
}
Run-Test @('frame','--size','1919x1080','--out',"$out\invalid") 2
Run-Test @('frame','--hdr','invalid','--out',"$out\invalid-hdr") 2
Run-Test @('frame','--out',"$out\gray") 2
Run-Test @('probe','--adapter','999') 3
$hashes = Get-ChildItem -LiteralPath $out -Recurse -File | ForEach-Object {
    @{ path = [IO.Path]::GetRelativePath($out,$_.FullName); sha256 = (Get-FileHash -LiteralPath $_.FullName).Hash }
}
$version = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
$summary = @{
    status='passed'; hdr_verified=$false; adapter=0
    os_build="$($version.CurrentBuild).$($version.UBR)"
    nvidia_smi=(& nvidia-smi --query-gpu=name,driver_version --format=csv,noheader)
    black=$black; white=$white; color_bar_centers=$samples
    unaligned_width=1918; unaligned_source_row_pitch=$pitchMeta.source_row_pitch
    frames=120; hashes=$hashes
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$out\verification.json" -Encoding utf8
Write-Output "Verification passed: $out\verification.json"
