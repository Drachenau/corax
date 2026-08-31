# SPDX-License-Identifier: Apache-2.0

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$GuiExecutable,

    [Parameter(Mandatory = $true)]
    [string]$ConsoleExecutable
)

$ErrorActionPreference = "Stop"

function Get-PeSubsystem {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $resolvedPath = (Resolve-Path -LiteralPath $Path).Path
    $stream = [System.IO.File]::OpenRead($resolvedPath)
    $reader = $null

    try {
        $reader = [System.IO.BinaryReader]::new($stream)
        if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5A4D) {
            throw "'$resolvedPath' is not a valid PE executable."
        }

        [void]$stream.Seek(0x3C, [System.IO.SeekOrigin]::Begin)
        $peHeaderOffset = $reader.ReadUInt32()
        $minimumLength = [long]$peHeaderOffset + 24 + 70
        if ($stream.Length -lt $minimumLength) {
            throw "'$resolvedPath' has a truncated PE optional header."
        }

        [void]$stream.Seek($peHeaderOffset, [System.IO.SeekOrigin]::Begin)
        if ($reader.ReadUInt32() -ne 0x00004550) {
            throw "'$resolvedPath' does not contain a PE signature."
        }

        [void]$stream.Seek($peHeaderOffset + 24, [System.IO.SeekOrigin]::Begin)
        $optionalHeaderMagic = $reader.ReadUInt16()
        if ($optionalHeaderMagic -ne 0x010B -and $optionalHeaderMagic -ne 0x020B) {
            throw "'$resolvedPath' has unsupported PE optional-header magic 0x$($optionalHeaderMagic.ToString('X4'))."
        }

        [void]$stream.Seek($peHeaderOffset + 24 + 68, [System.IO.SeekOrigin]::Begin)
        return [int]$reader.ReadUInt16()
    }
    finally {
        if ($null -ne $reader) {
            $reader.Dispose()
        }
        else {
            $stream.Dispose()
        }
    }
}

function Get-SubsystemName {
    param(
        [Parameter(Mandatory = $true)]
        [int]$Value
    )

    switch ($Value) {
        2 { return "Windows GUI" }
        3 { return "Windows CUI" }
        default { return "unknown" }
    }
}

$expectations = @(
    [PSCustomObject]@{ Path = $GuiExecutable; Expected = 2; Role = "production application" }
    [PSCustomObject]@{ Path = $ConsoleExecutable; Expected = 3; Role = "app integration test" }
)

$verificationFailed = $false
foreach ($expectation in $expectations) {
    try {
        $actual = Get-PeSubsystem -Path $expectation.Path
        $actualName = Get-SubsystemName -Value $actual
        $expectedName = Get-SubsystemName -Value $expectation.Expected
        Write-Output "$($expectation.Path): subsystem $actual ($actualName); expected $($expectation.Expected) ($expectedName)."

        if ($actual -ne $expectation.Expected) {
            Write-Output "::error file=$($expectation.Path)::$($expectation.Role) has PE subsystem $actual ($actualName); expected $($expectation.Expected) ($expectedName)."
            $verificationFailed = $true
        }
    }
    catch {
        Write-Output "::error file=$($expectation.Path)::Unable to verify the $($expectation.Role): $($_.Exception.Message)"
        $verificationFailed = $true
    }
}

if ($verificationFailed) {
    exit 1
}
