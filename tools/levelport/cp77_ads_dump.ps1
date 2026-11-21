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
# cp77_ads_dump.ps1 -Wk <wolvenkit dir> -Db <tweakdb.bin> -Out <ads.json>: CP2077's advertisement records from the
# game's tweakdb.bin, read in pwsh (PowerShell 7 on .NET 10) by WolvenKit's TweakDBService (record names from its
# embedded tweakdbstr, resource paths from its hash service). Writes one JSON file (UTF-8, CRLF):
#   source  the tweakdb.bin read
#   groups  every gamedataAdvertisementGroup_Record by name, with each of its fields (ads, included groups...)
#   ads     every gamedataAdvertisement_Record by name, with each of its fields (Resource: the .inkwidget's depot
#           path) and Definitions: each definition record's fields (Format, LibraryName)
# A name or path the services do not know is written as its hash (0x...). Read-only on the game.
# cp77_ads_fetch.py's dump step runs it; cp77_ads.py reads the result.
# (no [Parameter()] attributes: they make the script an advanced one, whose -Debug alias -db clashes with -Db)
param([string]$Wk, [string]$Db, [string]$Out)
$ErrorActionPreference = 'Stop'
if (-not $Wk -or -not $Db -or -not $Out) { throw 'usage: cp77_ads_dump.ps1 -Wk <wolvenkit dir> -Db <tweakdb.bin> -Out <ads.json>' }
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
$hs = New-Object WolvenKit.Common.Services.HashService
$svc = New-Object WolvenKit.Common.Services.TweakDBService($hs)
$svc.LoadDB($Db).GetAwaiter().GetResult()
"loaded: $($svc.IsLoaded); records $([WolvenKit.Common.Services.TweakDBService]::GetRecords().Count)"

function Name($id) {
    $h = [UInt64]$id
    $s = [WolvenKit.RED4.Types.Pools.TweakDBIDPool]::ResolveHash($h)
    if ($s) { return $s }
    return ('0x{0:X}' -f $h)
}
function Path($ra) {
    if ($null -eq $ra) { return $null }
    $h = [UInt64]$ra.DepotPath.GetRedHash()
    if ($h -eq 0) { return $null }
    $p = $hs.Get($h)
    if ($p) { return $p }
    return ('0x{0:X}' -f $h)
}
function Val($v) {
    if ($null -eq $v) { return $null }
    if ($v -is [WolvenKit.RED4.Types.TweakDBID]) { return (Name $v) }
    if ($v -is [WolvenKit.RED4.Types.IRedResourceAsyncReference] -or $v -is [WolvenKit.RED4.Types.IRedResourceReference]) { return (Path $v) }
    if ($v -is [System.Collections.IEnumerable] -and -not ($v -is [string])) { return @($v | ForEach-Object { Val $_ }) }
    return "$v"
}
function Rec($id) {
    $r = [WolvenKit.Common.Services.TweakDBService]::GetRecord($id)
    if ($null -eq $r) { return $null }
    $o = [ordered]@{ name = (Name $id); type = $r.GetType().Name }
    foreach ($p in $r.GetType().GetProperties()) {
        if ($p.DeclaringType -eq [WolvenKit.RED4.Types.RedBaseClass] -or $p.GetIndexParameters().Count) { continue }
        try { $o[$p.Name] = Val ($p.GetValue($r)) } catch { $o[$p.Name] = "<err>" }
    }
    return $o
}
$groups = [ordered]@{}
$ads = [ordered]@{}
foreach ($id in [WolvenKit.Common.Services.TweakDBService]::GetRecords()) {
    $ok = $null
    if (-not [WolvenKit.Common.Services.TweakDBService]::TryGetType($id, [ref]$ok)) { continue }
    if ($ok.Name -eq 'gamedataAdvertisementGroup_Record') { $groups[(Name $id)] = Rec $id }
    elseif ($ok.Name -eq 'gamedataAdvertisement_Record') {
        $a = Rec $id
        $defs = @()
        foreach ($d in @([WolvenKit.Common.Services.TweakDBService]::GetRecord($id).Definitions)) { if ($d) { $defs += ,(Rec $d) } }
        $a['Definitions'] = $defs
        $ads[(Name $id)] = $a
    }
}
"groups $($groups.Count), ads $($ads.Count)"
[ordered]@{ source = $Db; groups = $groups; ads = $ads } | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 $Out
"wrote $Out"
