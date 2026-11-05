param(
    [Parameter(Mandatory = $true)]
    [string]$Path,

    [Parameter(Mandatory = $true)]
    [ValidateSet('native-x64','native-arm64','x64-on-arm64-emulation')]
    [string]$ExpectedMode,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'

$resolved = (Resolve-Path -LiteralPath $Path).Path
$bytes = [System.IO.File]::ReadAllBytes($resolved)
if ($bytes.Length -lt 0x40) { throw 'File is too small to be a PE image.' }
if ($bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) { throw 'Missing MZ header.' }

$peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
if ($peOffset -lt 0 -or ($peOffset + 6) -gt $bytes.Length) { throw 'Invalid PE header offset.' }
if ($bytes[$peOffset] -ne 0x50 -or $bytes[$peOffset + 1] -ne 0x45 -or $bytes[$peOffset + 2] -ne 0 -or $bytes[$peOffset + 3] -ne 0) {
    throw 'Missing PE signature.'
}

$machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
$machineName = switch ($machine) {
    0x8664 { 'AMD64' }
    0xAA64 { 'ARM64' }
    0x014c { 'I386' }
    default { 'UNKNOWN' }
}

$os = Get-CimInstance Win32_OperatingSystem
$osArch = [string]$os.OSArchitecture
$isArm64Os = ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') -or ($osArch -match 'ARM')
$isX64Os = ($env:PROCESSOR_ARCHITECTURE -eq 'AMD64') -or ($osArch -match '64-bit' -and -not $isArm64Os)

$mode = 'unknown'
if ($machine -eq 0xAA64 -and $isArm64Os) { $mode = 'native-arm64' }
elseif ($machine -eq 0x8664 -and $isArm64Os) { $mode = 'x64-on-arm64-emulation' }
elseif ($machine -eq 0x8664 -and $isX64Os) { $mode = 'native-x64' }

$result = [ordered]@{
    schema_version = 1
    path = $resolved
    pe_machine = ('0x{0:X4}' -f $machine)
    pe_machine_name = $machineName
    os_architecture = $osArch
    processor_architecture = $env:PROCESSOR_ARCHITECTURE
    execution_mode = $mode
    expected_mode = $ExpectedMode
    matches_expected = ($mode -eq $ExpectedMode)
    sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $resolved).Hash.ToLowerInvariant()
}

$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $OutputPath -Encoding utf8
$result | ConvertTo-Json -Depth 5 | Write-Host

if (-not $result.matches_expected) {
    throw "PE/OS execution classification '$mode' does not match expected treatment '$ExpectedMode'."
}
