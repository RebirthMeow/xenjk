param(
    [Parameter(Mandatory=$true)][string]$BuildPath
)
$ErrorActionPreference = 'Stop'
$runnerRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$runnerBuild = if ([IO.Path]::IsPathRooted($BuildPath)) { [IO.Path]::GetFullPath($BuildPath) } else { [IO.Path]::GetFullPath((Join-Path $runnerRoot $BuildPath)) }
$runnerGame = Join-Path $runnerRoot 'game_directory\Jedi Academy\GameData'
$runnerStamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffffffZ')
$runnerArchive = Join-Path $runnerRoot ('.codex-archive\' + $runnerStamp)
function Assert-RunnerPath([string]$Path) {
    $resolved = [IO.Path]::GetFullPath($Path)
    if (-not $resolved.StartsWith($runnerRoot + '\',[StringComparison]::OrdinalIgnoreCase)) { throw "Outside workspace: $resolved" }
    $ancestor = $resolved
    while ($ancestor.Length -ge $runnerRoot.Length) {
        if (Test-Path -LiteralPath $ancestor) {
            $item = Get-Item -LiteralPath $ancestor -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point: $ancestor" }
        }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
    return $resolved
}
function Get-RunnerCopyHash($Copy) {
    if ($Copy.ContainsKey('Content')) {
        $hasher = [Security.Cryptography.SHA256]::Create()
        try { return [BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::ASCII.GetBytes($Copy.Content))).Replace('-', '') }
        finally { $hasher.Dispose() }
    }
    return (Get-FileHash -LiteralPath $Copy.Source -Algorithm SHA256).Hash
}
$runnerCopies = @(
    @{Source=(Join-Path $runnerBuild 'RelWithDebInfo\jampgamex86.dll'); Target=(Join-Path $runnerGame 'jampgamex86.dll')},
    @{Source=(Join-Path $runnerBuild 'RelWithDebInfo\jampgamex86.pdb'); Target=(Join-Path $runnerGame 'jampgamex86.pdb')},
    @{Source=(Join-Path $runnerBuild 'RelWithDebInfo\jampgamex86.dll'); Target=(Join-Path $runnerGame 'base\jampgamex86.dll')},
    @{Source=(Join-Path $runnerBuild 'RelWithDebInfo\jampgamex86.pdb'); Target=(Join-Path $runnerGame 'base\jampgamex86.pdb')},
    @{Source=(Join-Path $runnerRoot 'tools\fixtures\strafe_play\kejim_runner.bot'); Target=(Join-Path $runnerGame 'base\scripts\kejim_runner.bot')},
    @{Source=(Join-Path $runnerRoot 'tools\fixtures\strafe_play\kejim_runner.jkb'); Target=(Join-Path $runnerGame 'base\botfiles\kejim_runner.jkb')}
)
# The DLL must be the one its build checked: strafe_fixture_build.py writes <stamp>.sources.json with the
# binaries' hashes only after a complete build, so a failed or half-copied build has no manifest or another hash.
$runnerBuildManifest = Get-ChildItem -LiteralPath $runnerBuild -Filter '*.sources.json' -File | Sort-Object Name | Select-Object -Last 1
if (-not $runnerBuildManifest) { throw "No build manifest (*.sources.json) in ${runnerBuild}: not a complete build" }
$runnerExpected = (Get-Content -LiteralPath $runnerBuildManifest.FullName -Raw | ConvertFrom-Json).binaries.'jampgamex86.dll'
$runnerDll = Join-Path $runnerBuild 'RelWithDebInfo\jampgamex86.dll'
if (-not $runnerExpected -or -not (Test-Path -LiteralPath $runnerDll -PathType Leaf) -or
    (Get-FileHash -LiteralPath $runnerDll -Algorithm SHA256).Hash -ne $runnerExpected.ToUpperInvariant()) {
    throw "jampgamex86.dll does not match $($runnerBuildManifest.Name)"
}
$runnerArchive = Assert-RunnerPath $runnerArchive
if (Test-Path -LiteralPath $runnerArchive) { throw "Archive collision: $runnerArchive" }
foreach ($copy in $runnerCopies) {
    if (-not $copy.ContainsKey('Content')) {
        $copy.Source = Assert-RunnerPath $copy.Source
        if (-not (Test-Path -LiteralPath $copy.Source -PathType Leaf)) { throw "Missing source: $($copy.Source)" }
    }
    $copy.Target = Assert-RunnerPath $copy.Target
    $copy.Archive = Assert-RunnerPath (Join-Path $runnerArchive $copy.Target.Substring($runnerRoot.Length + 1))
    if (Test-Path -LiteralPath $copy.Archive) { throw "Archive collision: $($copy.Archive)" }
    $copy.Existed = Test-Path -LiteralPath $copy.Target
    if ($copy.Existed -and -not (Test-Path -LiteralPath $copy.Target -PathType Leaf)) { throw "Target is not a file: $($copy.Target)" }
}
$runnerManifest = @{
    timestamp_utc=$runnerStamp; reason='Install the user-requested normal Create Game Kejim runner'; build=$runnerBuild
    entries=@($runnerCopies | Where-Object {$_.Existed} | ForEach-Object {
        @{original_absolute_path=$_.Target; archived_path=$_.Archive; timestamp_utc=$runnerStamp; reason='Replaced by Kejim runner installation'}
    })
    installed=@($runnerCopies | ForEach-Object {@{path=$_.Target; sha256=(Get-RunnerCopyHash $_)}})
}
New-Item -ItemType Directory -Path $runnerArchive | Out-Null
$runnerManifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $runnerArchive 'manifest.json')
foreach ($copy in $runnerCopies) {
    if ($copy.Existed) {
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($copy.Archive)) -Force | Out-Null
        Move-Item -LiteralPath $copy.Target -Destination $copy.Archive
    }
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($copy.Target)) -Force | Out-Null
    if ($copy.ContainsKey('Content')) {
        [IO.File]::WriteAllText($copy.Target,$copy.Content,[Text.Encoding]::ASCII)
    } else {
        Copy-Item -LiteralPath $copy.Source -Destination $copy.Target
    }
    if ((Get-RunnerCopyHash $copy) -ne (Get-FileHash -LiteralPath $copy.Target -Algorithm SHA256).Hash) { throw "Installed hash mismatch: $($copy.Target)" }
}
Write-Output "Installed addrunner and matching symbols. Archive: $runnerArchive"
