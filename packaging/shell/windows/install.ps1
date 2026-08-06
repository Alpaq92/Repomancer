#Requires -Version 5
# Repomancer Windows Tier-0 shell menu installer (per-user, no admin needed).
#   .\install.ps1               # install, autodetecting repomancer.exe
#   .\install.ps1 -Bin C:\path\to\repomancer.exe
#   .\install.ps1 -Uninstall
param([string]$Bin, [switch]$Uninstall)

$ErrorActionPreference = 'Stop'
$root = 'HKCU:\Software\Classes\Directory'

if ($Uninstall) {
    Remove-Item "$root\shell\Repomancer" -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item "$root\Background\shell\Repomancer" -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host 'Repomancer shell menu removed.'
    return
}

if (-not $Bin) {
    $cmd = Get-Command repomancer.exe -ErrorAction SilentlyContinue
    if ($cmd) { $Bin = $cmd.Source }
}
if (-not $Bin -or -not (Test-Path $Bin)) {
    throw 'Could not find repomancer.exe. Pass -Bin C:\path\to\repomancer.exe'
}
$Bin = (Resolve-Path $Bin).Path

$reg = Join-Path $PSScriptRoot 'repomancer-menu.reg'
$tmp = Join-Path $env:TEMP 'repomancer-menu.reg'
# .reg wants doubled backslashes in values.
(Get-Content $reg -Raw).Replace('@REPOMANCER_BIN@', $Bin.Replace('\', '\\')) |
    Set-Content $tmp -Encoding Unicode
reg import $tmp
Remove-Item $tmp -Force
Write-Host "Repomancer shell menu installed for $Bin."
