param(
    [Parameter(Mandatory = $true)]
    [string]$WorkspaceRoot
)

$ErrorActionPreference = 'Stop'

$workspace = [System.IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd('\')
$source = [System.IO.Path]::GetFullPath((Join-Path $workspace 'build\aimfeedback-kejim-xen-4v4-20200829\base\screenshots'))
$workspacePrefix = $workspace + '\'

if (-not $source.StartsWith($workspacePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Screenshot path escaped the workspace: $source"
}
if (-not (Test-Path -LiteralPath $source -PathType Container)) {
    exit 0
}
$sourceItem = Get-Item -LiteralPath $source -Force
if (($sourceItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw "Refusing to archive a reparse-point screenshot directory: $source"
}

$timestamp = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssfffZ')
$archiveRoot = [System.IO.Path]::GetFullPath((Join-Path $workspace ".codex-archive\$timestamp"))
$destination = [System.IO.Path]::GetFullPath((Join-Path $archiveRoot 'build\aimfeedback-kejim-xen-4v4-20200829\base\screenshots'))
if (-not $destination.StartsWith($archiveRoot + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Archive destination escaped its timestamped root: $destination"
}
if (Test-Path -LiteralPath $destination) {
    throw "Archive destination already exists: $destination"
}

New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
Move-Item -LiteralPath $source -Destination $destination

$manifest = @(
    "timestamp_utc: $((Get-Date).ToUniversalTime().ToString('o'))"
    'reason: Preserve the previous Golan lesson screenshot set before a new capture.'
    ''
    "original: $source"
    "archived: $destination"
)
Set-Content -LiteralPath (Join-Path $archiveRoot 'manifest.txt') -Value $manifest -Encoding UTF8

Write-Host "Archived previous captures to:"
Write-Host "  $destination"
