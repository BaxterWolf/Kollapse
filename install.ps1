# Installs kollapse.exe / kollapsew.exe into %LOCALAPPDATA%\Programs\kollapse and puts that folder on the user PATH.
# Re-run after every build. Works while kollapse is running: the running copy is renamed to .old and replaced.
$ErrorActionPreference = 'Stop'
$dest = Join-Path $env:LOCALAPPDATA 'Programs\kollapse'
New-Item -ItemType Directory -Force $dest | Out-Null
Get-ChildItem $dest -Filter '*.old' | ForEach-Object {                   # leftovers from earlier updates
    try { Remove-Item $_.FullName -Force -ErrorAction Stop } catch {}    # still running: leave it for next time
}
foreach ($exe in 'kollapse.exe', 'kollapsew.exe') {
    $target = Join-Path $dest $exe
    if (Test-Path $target) {
        try { Remove-Item $target -Force -ErrorAction Stop }
        catch { Rename-Item $target ("$exe." + (Get-Date -Format 'yyyyMMddHHmmss') + '.old') }   # in use: move it aside
    }
    Copy-Item (Join-Path $PSScriptRoot $exe) $target
}

$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
if (-not $userPath) { $userPath = '' }
if (($userPath -split ';') -notcontains $dest) {
    $new = ($userPath.TrimEnd(';') + ';' + $dest).TrimStart(';')
    [Environment]::SetEnvironmentVariable('Path', $new, 'User')
    "Added $dest to your user PATH. Open a new PowerShell window, then type: kollapse"
} else {
    "Updated $dest (already on PATH). A kollapse that is already running keeps the old version until you restart it."
}
