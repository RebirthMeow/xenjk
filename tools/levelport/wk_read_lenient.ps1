# ===========================================================================
# Copyright (C) 2026, xenjk contributors
#
# This file is part of xenjk, an OpenJK derivative.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
# ===========================================================================
# wk_read_lenient.ps1 <in.streamingsector> <out.json> <wolvenkit dir>: read a CR2W file with WolvenKit 9.0.1's own reader in pwsh
# (.NET 10), answering its ParsingError event as handled (an RTTI type mismatch keeps the value it read instead of
# aborting the whole file), then write WolvenKit's usual JSON (RedFileDto via RedJsonSerializer). Logs every handled
# error. Read-only on the input; for files `convert serialize` refuses (exterior_-17_21_0_0: AccessPointControllerPS).
# cp2gltf.py's sectors stage runs it for every sector `convert serialize` left without JSON. On a sector the CLI
# reads, its Data section is byte-equal to the CLI's (the header lacks ArchiveFileName).
param([string]$In, [string]$Out, [Parameter(Mandatory = $true)][string]$Wk)
$ErrorActionPreference = 'Stop'
$wk = $Wk
[AppDomain]::CurrentDomain.add_AssemblyResolve({
    param($s, $e)
    $n = (New-Object Reflection.AssemblyName($e.Name)).Name
    $p = Join-Path $wk "$n.dll"
    if (Test-Path $p) { return [Reflection.Assembly]::LoadFrom($p) }
    return $null
})
$red4 = [Reflection.Assembly]::LoadFrom("$wk\WolvenKit.RED4.dll")
$common = [Reflection.Assembly]::LoadFrom("$wk\WolvenKit.Common.dll")
$global:handled = New-Object System.Collections.Generic.List[string]
$handler = [WolvenKit.RED4.Types.ParsingErrorEventHandler] {
    param($e)
    $msg = $e.GetType().Name
    if ($e -is [WolvenKit.RED4.Types.InvalidRTTIEventArgs]) { $msg += " $($e.PropertyName) expected $($e.ExpectedType) got $($e.ActualType)" }
    $global:handled.Add($msg)
    return $true
}
$fs = [IO.File]::OpenRead($In)
$reader = New-Object WolvenKit.RED4.Archive.IO.CR2WReader($fs)
$reader.add_ParsingError($handler)
$file = $null
$code = $reader.ReadFile([ref]$file, $true)
$fs.Dispose()
"read: $code, handled errors: $($global:handled.Count)"
$global:handled | Group-Object | ForEach-Object { "  $($_.Count) x $($_.Name)" }
if ($null -eq $file) { exit 2 }
$dtoType = $common.GetTypes() | Where-Object { $_.Name -eq 'RedFileDto' } | Select-Object -First 1
$serType = $common.GetTypes() | Where-Object { $_.Name -eq 'RedJsonSerializer' } | Select-Object -First 1
$ctor = $dtoType.GetConstructors() | Where-Object { $_.GetParameters().Count -ge 1 -and $_.GetParameters()[0].ParameterType.IsAssignableFrom($file.GetType()) } | Select-Object -First 1
$cargs = New-Object object[] $ctor.GetParameters().Count
$cargs[0] = $file
for ($i = 1; $i -lt $cargs.Length; $i++) { $p = $ctor.GetParameters()[$i]; if ($p.HasDefaultValue) { $cargs[$i] = $p.DefaultValue } }
$dto = $ctor.Invoke($cargs)
$m = $serType.GetMethods() | Where-Object { $_.Name -eq 'Serialize' -and $_.GetParameters().Count -ge 1 -and -not $_.IsGenericMethod } | Select-Object -First 1
$args_ = New-Object object[] $m.GetParameters().Count
$args_[0] = $dto
for ($i = 1; $i -lt $m.GetParameters().Count; $i++) { $p = $m.GetParameters()[$i]; if ($p.HasDefaultValue) { $args_[$i] = $p.DefaultValue } }
$json = $m.Invoke($null, $args_)
[IO.File]::WriteAllText($Out, $json, (New-Object Text.UTF8Encoding($false)))
"wrote $Out ($((Get-Item $Out).Length) bytes)"
