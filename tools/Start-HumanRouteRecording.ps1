param([string]$Build = '', [ValidateSet('red','blue')][string]$Team = 'red', [switch]$NoClient)
$ErrorActionPreference = 'Stop'
$env:PYTHONDONTWRITEBYTECODE = '1'
$recorderArgs = @((Join-Path $PSScriptRoot 'human_route_record.py'), '--team', $Team)
if ($Build) { $recorderArgs += @('--build', $Build) }
if ($NoClient) { $recorderArgs += '--no-client' }
& python @recorderArgs
if ($LASTEXITCODE -ne 0) { throw 'Route recording launcher failed. Retained logs are under .codex-research.' }
