param(
    [Parameter(Mandatory = $true)]
    [string]$TrackPath,
    [int]$Limit = 40,
    [int]$MinSeparationMsec = 800
)

$ErrorActionPreference = 'Stop'

function Normalize-Angle([double]$Angle) {
    while ($Angle -gt 180.0) { $Angle -= 360.0 }
    while ($Angle -lt -180.0) { $Angle += 360.0 }
    return $Angle
}

$resolvedTrack = [IO.Path]::GetFullPath($TrackPath)
if (!(Test-Path -LiteralPath $resolvedTrack -PathType Leaf)) {
    throw "Track cache does not exist: $resolvedTrack"
}

$stream = [IO.File]::OpenRead($resolvedTrack)
$reader = [IO.BinaryReader]::new($stream)
$entries = [System.Collections.Generic.List[object]]::new()
try {
    $magic = $reader.ReadInt32()
    $version = $reader.ReadInt32()
    $entrySize = $reader.ReadInt32()
    if ($magic -ne 0x31544641 -or $version -ne 2 -or $entrySize -ne 84) {
        throw "Unsupported FC track header: magic=$('{0:X8}' -f $magic), version=$version, entrySize=$entrySize"
    }
    while ($stream.Position -lt $stream.Length) {
        $entries.Add([pscustomobject]@{
            Time = $reader.ReadInt32()
            Carrier = $reader.ReadInt32()
            Origin = @($reader.ReadSingle(), $reader.ReadSingle(), $reader.ReadSingle())
            Mins = @($reader.ReadSingle(), $reader.ReadSingle(), $reader.ReadSingle())
            Maxs = @($reader.ReadSingle(), $reader.ReadSingle(), $reader.ReadSingle())
            Alive = $reader.ReadInt32()
            Shooter = @($reader.ReadSingle(), $reader.ReadSingle(), $reader.ReadSingle())
            Angles = @($reader.ReadSingle(), $reader.ReadSingle(), $reader.ReadSingle())
            Viewheight = $reader.ReadInt32()
            Weapon = $reader.ReadInt32()
            Health = $reader.ReadInt32()
        })
    }
}
finally {
    $reader.Dispose()
    $stream.Dispose()
}

$raw = [System.Collections.Generic.List[object]]::new()
for ($index = 0; $index -lt $entries.Count - 15; $index++) {
    $entry = $entries[$index]
    if ($entry.Carrier -lt 0 -or !$entry.Alive -or $entry.Health -le 0 -or $entry.Weapon -ne 10) {
        continue
    }

    $future = @(2, 4, 6, 10, 14) | ForEach-Object { $entries[$index + $_] }
    if ($future.Where({ $_.Carrier -ne $entry.Carrier -or !$_.Alive }).Count -gt 0) {
        continue
    }

    $targetX = $entry.Origin[0]
    $targetY = $entry.Origin[1]
    $targetZ = $entry.Origin[2] + ($entry.Mins[2] + $entry.Maxs[2]) * 0.5
    $shooterX = $entry.Shooter[0]
    $shooterY = $entry.Shooter[1]
    $shooterZ = $entry.Shooter[2] + $entry.Viewheight
    $dx = $targetX - $shooterX
    $dy = $targetY - $shooterY
    $dz = $targetZ - $shooterZ
    $horizontalRange = [Math]::Sqrt($dx * $dx + $dy * $dy)
    $range = [Math]::Sqrt($horizontalRange * $horizontalRange + $dz * $dz)
    if ($range -lt 280.0 -or $range -gt 1200.0 -or $horizontalRange -lt 1.0) {
        continue
    }

    $targetYaw = [Math]::Atan2($dy, $dx) * 180.0 / [Math]::PI
    $targetPitch = -[Math]::Atan2($dz, $horizontalRange) * 180.0 / [Math]::PI
    $yawDelta = Normalize-Angle ($targetYaw - $entry.Angles[1])
    $pitchDelta = Normalize-Angle ($targetPitch - $entry.Angles[0])
    if ([Math]::Abs($yawDelta) -gt 48.0 -or [Math]::Abs($pitchDelta) -gt 30.0) {
        continue
    }

    $samples = @($entry) + $future[0..2]
    $velocities = [System.Collections.Generic.List[object]]::new()
    for ($velocityIndex = 0; $velocityIndex -lt 3; $velocityIndex++) {
        $before = $samples[$velocityIndex]
        $after = $samples[$velocityIndex + 1]
        $seconds = ($after.Time - $before.Time) * 0.001
        if ($seconds -le 0.0) { break }
        $velocities.Add([pscustomobject]@{
            X = ($after.Origin[0] - $before.Origin[0]) / $seconds
            Y = ($after.Origin[1] - $before.Origin[1]) / $seconds
            Z = ($after.Origin[2] - $before.Origin[2]) / $seconds
            Dt = $seconds
        })
    }
    if ($velocities.Count -ne 3) { continue }

    $horizontalSpeeds = $velocities | ForEach-Object { [Math]::Sqrt($_.X * $_.X + $_.Y * $_.Y) }
    if (($horizontalSpeeds | Measure-Object -Minimum).Minimum -lt 300.0 -or
        ($horizontalSpeeds | Measure-Object -Maximum).Maximum -gt 950.0) {
        continue
    }
    $headingDot1 = ($velocities[0].X * $velocities[1].X + $velocities[0].Y * $velocities[1].Y) /
        ($horizontalSpeeds[0] * $horizontalSpeeds[1])
    $headingDot2 = ($velocities[1].X * $velocities[2].X + $velocities[1].Y * $velocities[2].Y) /
        ($horizontalSpeeds[1] * $horizontalSpeeds[2])
    if ($headingDot1 -lt 0.96 -or $headingDot2 -lt 0.96) { continue }

    $acceleration1 = ($velocities[1].Z - $velocities[0].Z) /
        (($velocities[0].Dt + $velocities[1].Dt) * 0.5)
    $acceleration2 = ($velocities[2].Z - $velocities[1].Z) /
        (($velocities[1].Dt + $velocities[2].Dt) * 0.5)
    $verticalSpeed = $velocities[0].Z
    if ([Math]::Abs($verticalSpeed) -lt 130.0 -or
        $acceleration1 -gt -250.0 -or $acceleration1 -lt -1500.0 -or
        $acceleration2 -gt -250.0 -or $acceleration2 -lt -1500.0) {
        continue
    }

    $losX = $dx / $horizontalRange
    $losY = $dy / $horizontalRange
    $awaySpeed = $velocities[0].X * $losX + $velocities[0].Y * $losY
    $lateralSpeed = [Math]::Sqrt([Math]::Max(0.0,
        $horizontalSpeeds[0] * $horizontalSpeeds[0] - $awaySpeed * $awaySpeed))
    if ($lateralSpeed -lt 220.0) { continue }

    $gravityError = [Math]::Abs($acceleration1 + 800.0) + [Math]::Abs($acceleration2 + 800.0)
    $futureDz = $future[4].Origin[2] - $entry.Origin[2]
    $score = 2.0 * (($headingDot1 + $headingDot2) * 0.5) +
        1.5 * [Math]::Min($lateralSpeed, 650.0) / 650.0 -
        0.65 * [Math]::Abs($range - 512.0) / 512.0 -
        0.35 * $gravityError / 1600.0 -
        0.15 * [Math]::Abs($yawDelta) / 48.0

    $raw.Add([pscustomobject]@{
        Time = $entry.Time
        Carrier = $entry.Carrier
        Range = [Math]::Round($range)
        Speed = [Math]::Round($horizontalSpeeds[0])
        Lateral = [Math]::Round($lateralSpeed)
        Away = [Math]::Round($awaySpeed)
        Vz = [Math]::Round($verticalSpeed)
        Az1 = [Math]::Round($acceleration1)
        Az2 = [Math]::Round($acceleration2)
        FutureDz = [Math]::Round($futureDz)
        Dot = [Math]::Round([Math]::Min($headingDot1, $headingDot2), 2)
        Yaw = [Math]::Round($yawDelta, 1)
        Pitch = [Math]::Round($pitchDelta, 1)
        Score = [Math]::Round($score, 3)
    })
}

$chosen = [System.Collections.Generic.List[object]]::new()
foreach ($candidate in ($raw | Sort-Object Score -Descending)) {
    $nearExisting = $chosen.Where({
        [Math]::Abs($_.Time - $candidate.Time) -lt $MinSeparationMsec
    }).Count -gt 0
    if (!$nearExisting) { $chosen.Add($candidate) }
    if ($chosen.Count -ge $Limit) { break }
}

Write-Output "TRACK_ENTRIES=$($entries.Count) RAW_JUMP_FRAMES=$($raw.Count) SELECTED=$($chosen.Count)"
$chosen | Format-Table -AutoSize
