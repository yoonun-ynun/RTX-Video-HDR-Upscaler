param([string]$Configuration = 'Release', [string]$BuildDirectory, [string]$FFmpegRoot)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (!$BuildDirectory) { $BuildDirectory = Join-Path $repo build }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) { $cmakePath = $cmakeCommand.Source }
else {
    $candidates = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\2022\*\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' -ErrorAction SilentlyContinue
    $cmakePath = $candidates | Select-Object -First 1 -ExpandProperty FullName
}
if (!$cmakePath) { throw 'CMake was not found. Install Visual Studio C++ CMake tools.' }
function Invoke-CMake([string[]]$Arguments) {
    # Some hosts supply both Path and PATH. MSBuild's .NET Framework child launcher rejects this.
    # Normalize only the child environment; do not modify machine/user environment settings.
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $cmakePath
    $start.UseShellExecute = $false
    $childEnvironment = @{}
    foreach ($item in [Environment]::GetEnvironmentVariables().GetEnumerator()) {
        $childEnvironment[$item.Key] = $item.Value
    }
    $start.Environment.Clear()
    foreach ($item in $childEnvironment.GetEnumerator()) { $start.Environment[$item.Key] = $item.Value }
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    $process = [System.Diagnostics.Process]::Start($start)
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw "CMake failed: $($process.ExitCode)" }
}
Invoke-CMake @('--fresh', '-S', $repo, '-B', $BuildDirectory, '-G', 'Visual Studio 17 2022', '-A', 'x64', "-DFFMPEG_ROOT=$FFmpegRoot")
Invoke-CMake @('--build', $BuildDirectory, '--config', $Configuration)
if ($Configuration -eq 'Release') { & "$PSScriptRoot\build-gui.ps1" -OutputDirectory (Join-Path $BuildDirectory Release) }

if ($FFmpegRoot) { Get-ChildItem -LiteralPath (Join-Path $FFmpegRoot bin) -Filter "*.dll" | Copy-Item -Destination (Join-Path $BuildDirectory $Configuration) }
