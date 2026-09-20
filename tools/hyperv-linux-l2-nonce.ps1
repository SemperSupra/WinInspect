param([int]$MemoryMB=768,[int]$HeartbeatTimeoutSeconds=75,[int]$GuestSettleSeconds=35)
$ErrorActionPreference='Stop'
$name='l2-'+[guid]::NewGuid().ToString('N').Substring(0,8)
$hostArch=[Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
$arch=if($hostArch -eq 'Arm64'){'arm64'}else{'amd64'}
$receiptPath=Join-Path $env:RUNNER_TEMP "hyperv-linux-l2-$arch.json"
$state=[ordered]@{
  schema='hyperv-linux-l2-nonce/v3'
  architecture=$arch
  vmName=$name
  stage='init'
  stageTimestamp=(Get-Date).ToUniversalTime().ToString('o')
  l2Executed=$false
  oracleSatisfied=$false
  classification='IN_PROGRESS'
}

function Save-State {
  $state | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 $receiptPath
}
function Set-Stage([string]$stage) {
  $state.stage=$stage
  $state.stageTimestamp=(Get-Date).ToUniversalTime().ToString('o')
  Write-Host "L2_STAGE=$stage"
  Save-State
}
function Invoke-Curl([string]$url,[string]$out) {
  & curl.exe -L --fail --silent --show-error --retry 2 --retry-delay 2 --output $out $url
  if($LASTEXITCODE -ne 0){ throw "curl failed ($LASTEXITCODE): $url" }
}

Save-State

# Resource discipline: x64 first. ARM64 gets the same architecture-neutral oracle only after
# x64 proves that the image/seed/receipt path itself works.
if($arch -eq 'arm64'){
  $state.classification='DEFERRED_UNTIL_X64_ORACLE'
  $state.reason='ARM64 intentionally gated until the x64 direct-VHD + writable NoCloud seed oracle is proven.'
  Set-Stage 'deferred-arm64'
  exit 0
}

$workDrive=Get-PSDrive -Name D -ErrorAction SilentlyContinue
if($workDrive -and $workDrive.Free -gt 8GB){$root="D:\$name"}else{$root=Join-Path $env:RUNNER_TEMP $name}
New-Item -ItemType Directory -Path $root -Force|Out-Null

# Prior-art-first and deliberately small: Alpine publishes a direct UEFI Azure VHD, so there is
# no archive expansion, image conversion, or custom bootloader. Pin exact release + verify SHA512.
$release='3.24.1'
$base='https://dl-cdn.alpinelinux.org/alpine/v3.24/releases/cloud'
$imageName="azure_alpine-$release-x86_64-uefi-cloudinit-r0.vhd"
$image=Join-Path $root $imageName
$imageVhdx=Join-Path $root ([IO.Path]::GetFileNameWithoutExtension($imageName)+'.vhdx')
$sumFile="$image.sha512"
$seed=Join-Path $root 'cidata.vhdx'
$memoryBytes=[int64]$MemoryMB * 1MB
$vm=$null

try {
  Set-Stage 'download-image'
  $sw=[Diagnostics.Stopwatch]::StartNew()
  Invoke-Curl "$base/$imageName" $image
  $sw.Stop()
  $state.imageBytes=(Get-Item $image).Length
  $state.imageDownloadSeconds=[math]::Round($sw.Elapsed.TotalSeconds,3)
  Save-State

  Set-Stage 'verify-image'
  Invoke-Curl "$base/$imageName.sha512" $sumFile
  $expected=((Get-Content $sumFile -Raw).Trim() -split '\s+')[0].ToLowerInvariant()
  $actual=(Get-FileHash $image -Algorithm SHA512).Hash.ToLowerInvariant()
  if(-not $expected -or $expected.Length -ne 128){throw "Invalid published SHA512 material for $imageName"}
  if($actual -ne $expected){throw "Alpine VHD SHA512 mismatch expected=$expected actual=$actual"}
  $state.source='Alpine Linux official Azure UEFI cloud-init VHD'
  $state.release=$release
  $state.image=$imageName
  $state.imageSha512=$actual
  Save-State

  Set-Stage 'convert-vhdx'
  $convertSw=[Diagnostics.Stopwatch]::StartNew()
  Convert-VHD -Path $image -DestinationPath $imageVhdx -VHDType Dynamic
  $convertSw.Stop()
  $state.convertedVhdxBytes=(Get-Item $imageVhdx).Length
  $state.convertSeconds=[math]::Round($convertSw.Elapsed.TotalSeconds,3)
  Save-State

  Set-Stage 'create-seed'
  New-VHD -Path $seed -Fixed -SizeBytes 64MB|Out-Null
  $seedDisk=Mount-VHD -Path $seed -PassThru|Get-Disk
  Initialize-Disk -Number $seedDisk.Number -PartitionStyle MBR|Out-Null
  $seedPart=New-Partition -DiskNumber $seedDisk.Number -UseMaximumSize -AssignDriveLetter
  Format-Volume -Partition $seedPart -FileSystem FAT32 -NewFileSystemLabel CIDATA -Confirm:$false|Out-Null
  $seedRoot="$($seedPart.DriveLetter):\"
  @"
instance-id: $name
local-hostname: $name
"@ | Set-Content -Encoding ascii (Join-Path $seedRoot 'meta-data')
  @'
#cloud-config
bootcmd:
  - |
    set -eu
    dev="$(blkid -L CIDATA)"
    test -n "$dev"
    mkdir -p /mnt/cidata-rw
    mount -o rw "$dev" /mnt/cidata-rw || mount -o remount,rw "$dev" /mnt/cidata-rw
    cat /proc/sys/kernel/random/uuid > /mnt/cidata-rw/guest-nonce.txt
    cat /proc/sys/kernel/random/boot_id > /mnt/cidata-rw/guest-boot-id.txt
    uname -a > /mnt/cidata-rw/guest-uname.txt
    ip -o addr > /mnt/cidata-rw/guest-ip.txt 2>&1 || true
    ip route > /mnt/cidata-rw/guest-route.txt 2>&1 || true
    sync
'@ | Set-Content -Encoding ascii (Join-Path $seedRoot 'user-data')
  Dismount-VHD -Path $seed

  Set-Stage 'create-vm'
  $vm=New-VM -Name $name -Generation 2 -MemoryStartupBytes $memoryBytes -VHDPath $imageVhdx
  Set-VMProcessor -VMName $name -Count 1
  Set-VMFirmware -VMName $name -EnableSecureBoot Off
  Add-VMHardDiskDrive -VMName $name -ControllerType SCSI -Path $seed
  $osDisk=Get-VMHardDiskDrive -VMName $name|Where-Object Path -eq $imageVhdx|Select-Object -First 1
  if($osDisk){Set-VMFirmware -VMName $name -FirstBootDevice $osDisk}
  $switch=Get-VMSwitch|Where-Object{$_.Name -eq 'Default Switch'}|Select-Object -First 1
  if(-not $switch){$switch=Get-VMSwitch|Select-Object -First 1}
  if($switch){
    Connect-VMNetworkAdapter -VMName $name -SwitchName $switch.Name
    $state.networkSwitch=$switch.Name
  }
  $state.vmCreated=$true
  Save-State

  Set-Stage 'start-vm'
  Start-VM -Name $name
  $state.vmStarted=$true
  Save-State

  Set-Stage 'wait-heartbeat'
  $deadline=(Get-Date).AddSeconds($HeartbeatTimeoutSeconds)
  $heartbeatSeen=$false
  while((Get-Date) -lt $deadline){
    $hb=Get-VMIntegrationService -VMName $name -Name 'Heartbeat' -ErrorAction SilentlyContinue
    if($hb -and $hb.PrimaryStatusDescription -eq 'OK'){
      $heartbeatSeen=$true
      break
    }
    Start-Sleep -Seconds 3
  }
  $state.heartbeatSeen=$heartbeatSeen
  $state.vmState=(Get-VM -Name $name).State.ToString()
  $state.observedGuestIPs=@((Get-VMNetworkAdapter -VMName $name -ErrorAction SilentlyContinue).IPAddresses|Where-Object{$_})
  Save-State

  if($heartbeatSeen -and $GuestSettleSeconds -gt 0){
    Set-Stage 'guest-settle'
    Start-Sleep -Seconds $GuestSettleSeconds
  }

  Set-Stage 'stop-vm'
  Stop-VM -Name $name -TurnOff -Force -ErrorAction SilentlyContinue
  Start-Sleep -Seconds 2

  Set-Stage 'read-guest-receipt'
  $seedDisk=Mount-VHD -Path $seed -PassThru|Get-Disk
  $vol=$seedDisk|Get-Partition|Get-Volume|Where-Object FileSystemLabel -eq 'CIDATA'|Select-Object -First 1
  $guestNonce=$null;$bootId=$null;$uname=$null;$guestIp=$null;$guestRoute=$null
  if($vol -and $vol.DriveLetter){
    $receiptRoot="$($vol.DriveLetter):\"
    foreach($pair in @(
      @('guest-nonce.txt','guestNonce'),
      @('guest-boot-id.txt','guestBootId'),
      @('guest-uname.txt','guestUname'),
      @('guest-ip.txt','guestIpObservation'),
      @('guest-route.txt','guestRouteObservation')
    )){
      $p=Join-Path $receiptRoot $pair[0]
      if(Test-Path $p){$state[$pair[1]]=(Get-Content $p -Raw).Trim()}
    }
  }
  Dismount-VHD -Path $seed

  $validGuid=$false
  if($state.guestNonce){
    $tmp=[guid]::Empty
    $validGuid=[guid]::TryParse([string]$state.guestNonce,[ref]$tmp)
  }
  $state.l2Executed=$validGuid
  $state.oracleSatisfied=$validGuid
  $state.classification=$(if($validGuid){'L2_EXECUTION_PROVEN'}elseif($heartbeatSeen){'GUEST_EXECUTION_SEEN_NONCE_NOT_RECOVERED'}else{'GUEST_BOOT_NOT_PROVEN'})
  $state.reason=$(if($validGuid){
    'Guest-generated UUID was written to the writable NoCloud seed disk and recovered by L1.'
  }elseif($heartbeatSeen){
    'Hyper-V heartbeat became healthy, proving guest execution, but the NoCloud write-back oracle did not produce a valid nonce.'
  }else{
    'The VM control plane started the VM but no healthy Hyper-V heartbeat or guest nonce was observed in the bounded window.'
  })
  Set-Stage 'complete'
} catch {
  $state.classification='ENVIRONMENT_OR_HARNESS_FAILURE'
  $state.reason=$_.Exception.Message
  $state.failureType=$_.Exception.GetType().FullName
  Set-Stage 'failed'
} finally {
  if($vm){
    Stop-VM -Name $name -TurnOff -Force -ErrorAction SilentlyContinue
    Remove-VM -Name $name -Force -ErrorAction SilentlyContinue
  }
  Dismount-VHD -Path $seed -ErrorAction SilentlyContinue
  Save-State
  Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
}
