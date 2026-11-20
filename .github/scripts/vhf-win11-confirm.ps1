param(
  [string]$PackageDir = $PSScriptRoot,
  [string]$EvidenceDir = (Join-Path $PSScriptRoot 'win11-evidence')
)

$ErrorActionPreference = 'Stop'
$PackageDir = (Resolve-Path $PackageDir).Path
$EvidenceDir = (New-Item -ItemType Directory -Force -Path $EvidenceDir).FullName
$hardwareId = 'ROOT\WININSPECT\VHFPROBE'
$instanceId = $null
$publishedName = $null
$certThumbprint = $null

function Write-JsonFile([object]$Value, [string]$Name, [int]$Depth = 8) {
  $Value | ConvertTo-Json -Depth $Depth | Set-Content (Join-Path $EvidenceDir $Name) -Encoding utf8
}

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  throw 'Run this confirmation from an elevated PowerShell session inside the disposable WinBot clone.'
}

$required = @(
  'WinInspectVhfProbe.inf',
  'WinInspectVhfProbe.cat',
  'WinInspectVhfProbe.sys',
  'wininspect-vhf-test.cer',
  'devgen.exe',
  'win32_input_probe.exe',
  'sendinput_control.exe',
  'vhf-common-probe.ps1',
  'handoff-manifest.json'
)
$missing = @($required | Where-Object { -not (Test-Path (Join-Path $PackageDir $_)) })
if ($missing.Count -gt 0) { throw "Missing handoff files: $($missing -join ', ')" }

$os = Get-CimInstance Win32_OperatingSystem
$secureBoot = try { [bool](Confirm-SecureBootUEFI -ErrorAction Stop) } catch { $null }
$bcd = (& bcdedit.exe /enum '{current}' 2>&1 | Out-String)
$testSigning = if ($bcd -match '(?im)^\s*testsigning\s+Yes\s*$') { $true } elseif ($bcd -match '(?im)^\s*testsigning\s+No\s*$') { $false } else { $null }
$manifest = Get-Content (Join-Path $PackageDir 'handoff-manifest.json') -Raw | ConvertFrom-Json
Write-JsonFile ([ordered]@{
  experiment = 'wininspect-381-vhf-win11-client-confirmation'
  started_at = (Get-Date).ToUniversalTime().ToString('o')
  windows_caption = $os.Caption
  windows_version = $os.Version
  windows_build = $os.BuildNumber
  product_type = $os.ProductType
  secure_boot = $secureBoot
  test_signing = $testSigning
  process_is_admin = $true
  hardware_id = $hardwareId
  handoff = $manifest
}) 'environment.json' 12
$bcd | Set-Content (Join-Path $EvidenceDir 'bcd-current.txt') -Encoding utf8

$result = [ordered]@{
  classification = 'not-attempted'
  signature_status = $null
  published_name = $null
  source_instance_id = $null
  source_status = $null
  source_problem = $null
  hid_child_count = 0
  input = $null
  cleanup = @()
}

try {
  $certPath = Join-Path $PackageDir 'wininspect-vhf-test.cer'
  $rootCert = Import-Certificate -FilePath $certPath -CertStoreLocation 'Cert:\LocalMachine\Root'
  $publisherCert = Import-Certificate -FilePath $certPath -CertStoreLocation 'Cert:\LocalMachine\TrustedPublisher'
  $certThumbprint = $rootCert.Thumbprint

  $cat = Join-Path $PackageDir 'WinInspectVhfProbe.cat'
  $sig = Get-AuthenticodeSignature -FilePath $cat
  $result.signature_status = [string]$sig.Status
  if ($sig.Status -ne 'Valid') {
    $result.classification = 'handoff-catalog-signature-not-valid'
    throw "Catalog signature status is $($sig.Status)"
  }

  $inf = Join-Path $PackageDir 'WinInspectVhfProbe.inf'
  $stageLines = & pnputil.exe /add-driver $inf 2>&1
  $stageCode = $LASTEXITCODE
  $stageLines | Set-Content (Join-Path $EvidenceDir 'stage.txt') -Encoding utf8
  $published = [regex]::Match(($stageLines -join [Environment]::NewLine), '(?im)^Published Name\s*:\s*(oem\d+\.inf)\s*$')
  if ($published.Success) {
    $publishedName = $published.Groups[1].Value
    $result.published_name = $publishedName
  }
  if ($stageCode -ne 0) {
    $result.classification = 'win11-signed-package-stage-failed'
    throw "pnputil stage failed: $stageCode"
  }

  Copy-Item (Join-Path $PackageDir 'win32_input_probe.exe') $EvidenceDir -Force
  Copy-Item (Join-Path $PackageDir 'sendinput_control.exe') $EvidenceDir -Force

  $devgenLines = & (Join-Path $PackageDir 'devgen.exe') /add /bus ROOT /hardwareid $hardwareId 2>&1
  $devgenCode = $LASTEXITCODE
  $devgenLines | Set-Content (Join-Path $EvidenceDir 'devgen.txt') -Encoding utf8
  if ($devgenCode -ne 0) {
    $result.classification = 'win11-root-devnode-create-failed'
    throw "devgen failed: $devgenCode"
  }
  $instanceMatch = [regex]::Match(($devgenLines -join [Environment]::NewLine), '(?im)Device Instance ID:\s*(ROOT\\DEVGEN\\\{[^\r\n]+\})')
  if (-not $instanceMatch.Success) {
    $result.classification = 'win11-devgen-instance-id-not-parsed'
    throw 'Could not parse devgen source instance ID.'
  }
  $instanceId = $instanceMatch.Groups[1].Value.Trim()
  $result.source_instance_id = $instanceId

  $installLines = & pnputil.exe /add-driver $inf /install 2>&1
  $installCode = $LASTEXITCODE
  $installLines | Set-Content (Join-Path $EvidenceDir 'install.txt') -Encoding utf8
  if ($installCode -ne 0) {
    $result.classification = 'win11-driver-bind-start-failed'
    throw "pnputil install failed: $installCode"
  }

  $childIds = @()
  for ($i = 0; $i -lt 30; $i++) {
    $prop = Get-PnpDeviceProperty -InstanceId $instanceId -KeyName 'DEVPKEY_Device_Children' -ErrorAction SilentlyContinue
    if ($prop -and $prop.Data) {
      $childIds = @($prop.Data)
      if ($childIds.Count -gt 0) { break }
    }
    Start-Sleep -Milliseconds 500
  }

  $source = Get-PnpDevice -InstanceId $instanceId -ErrorAction SilentlyContinue
  $result.source_status = if ($source) { [string]$source.Status } else { $null }
  $result.source_problem = if ($source) { [int]$source.Problem } else { $null }
  $children = @(
    foreach ($childId in $childIds) {
      $device = Get-PnpDevice -InstanceId $childId -ErrorAction SilentlyContinue
      if ($device) {
        $ids = @((Get-PnpDeviceProperty -InstanceId $childId -KeyName 'DEVPKEY_Device_HardwareIds' -ErrorAction SilentlyContinue).Data)
        [pscustomobject]@{
          Status = $device.Status
          Class = $device.Class
          FriendlyName = $device.FriendlyName
          InstanceId = $device.InstanceId
          Problem = $device.Problem
          HardwareIds = $ids
        }
      }
    }
  )
  Write-JsonFile $children 'source-children.json' 8
  $vhfChildren = @($children | Where-Object {
    $_.Class -eq 'HIDClass' -and (($_.HardwareIds -join ';') -match 'VID_1209.*PID_0266')
  })
  $result.hid_child_count = $vhfChildren.Count

  if (-not $source -or $source.Status -ne 'OK') {
    $result.classification = 'win11-source-device-not-ok'
    throw 'VHF source device did not reach Status OK.'
  }
  if ($vhfChildren.Count -lt 1) {
    $result.classification = 'win11-vhf-child-not-enumerated'
    throw 'VHF child did not enumerate.'
  }

  & (Join-Path $PackageDir 'vhf-common-probe.ps1') -Mode Observe -EvidenceDir $EvidenceDir
  $observeCode = $LASTEXITCODE
  if (Test-Path (Join-Path $EvidenceDir 'input-result.json')) {
    $result.input = Get-Content (Join-Path $EvidenceDir 'input-result.json') -Raw | ConvertFrom-Json
  }
  if ($observeCode -ne 0 -or -not $result.input -or $result.input.classification -ne 'vhf-report-observed-with-paired-controls') {
    $result.classification = 'win11-vhf-input-not-confirmed'
    throw "VHF input observation failed: $observeCode"
  }

  $result.classification = 'win11-vhf-report-observed-with-paired-controls'
}
catch {
  $_ | Out-String | Set-Content (Join-Path $EvidenceDir 'failure.txt') -Encoding utf8
}
finally {
  if ($instanceId) {
    $lines = & pnputil.exe /remove-device $instanceId 2>&1
    $result.cleanup += @($lines | ForEach-Object { "$_" })
  }
  if ($publishedName) {
    $lines = & pnputil.exe /delete-driver $publishedName /uninstall /force 2>&1
    $result.cleanup += @($lines | ForEach-Object { "$_" })
  }
  if ($certThumbprint) {
    Remove-Item "Cert:\LocalMachine\TrustedPublisher\$certThumbprint" -Force -ErrorAction SilentlyContinue
    Remove-Item "Cert:\LocalMachine\Root\$certThumbprint" -Force -ErrorAction SilentlyContinue
  }
  $result.cleanup | Set-Content (Join-Path $EvidenceDir 'cleanup.txt') -Encoding utf8
  Write-JsonFile $result 'win11-result.json' 12
  Get-Content (Join-Path $EvidenceDir 'win11-result.json')
}

if ($result.classification -ne 'win11-vhf-report-observed-with-paired-controls') { exit 1 }
