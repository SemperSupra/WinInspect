param([int]$MemoryMB=1024,[int]$TimeoutSeconds=120)
$ErrorActionPreference='Stop'
$name='l2-'+[guid]::NewGuid().ToString('N').Substring(0,8)
$hostArch=[Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
$arch=if($hostArch -eq 'Arm64'){'arm64'}else{'amd64'}

function Write-Receipt($receipt) {
  $out=Join-Path $env:RUNNER_TEMP "hyperv-linux-l2-$arch.json"
  $receipt|ConvertTo-Json -Depth 8|Set-Content -Encoding UTF8 $out
  $receipt|ConvertTo-Json -Depth 8
}

# Resource discipline: qualify x64 first; ARM64 follows only after the architecture-independent
# boot/seed/oracle path has been proven, avoiding a second ~600 MiB image transfer while debugging.
if($arch -eq 'arm64'){
  Write-Receipt ([ordered]@{
    schema='hyperv-linux-l2-nonce/v2'; architecture=$arch; l2Executed=$false;
    oracleSatisfied=$false; classification='DEFERRED_UNTIL_X64_ORACLE';
    reason='ARM64 intentionally gated until the x64 prior-art VHD + NoCloud nonce path is proven.'
  })
  exit 0
}

$workDrive=Get-PSDrive -Name D -ErrorAction SilentlyContinue
if($workDrive -and $workDrive.Free -gt 20GB){$root="D:\$name"}else{$root=Join-Path $env:RUNNER_TEMP $name}
New-Item -ItemType Directory -Path $root -Force|Out-Null

# Canonical-provided, Azure-ready Ubuntu VHD: already UEFI/Hyper-V/Azure oriented.
# Pin the released build directory; verify the archive against Canonical's published SHA256SUMS.
$release='20260911'
$base="https://cloud-images.ubuntu.com/releases/noble/release-$release"
$archiveName='ubuntu-24.04-server-cloudimg-amd64-azure.vhd.tar.gz'
$archive=Join-Path $root $archiveName
$sums=Join-Path $root 'SHA256SUMS'
$seed=Join-Path $root 'cidata.vhdx'
$memoryBytes=[int64]$MemoryMB * 1MB
$vm=$null

try {
  Invoke-WebRequest "$base/$archiveName" -OutFile $archive
  Invoke-WebRequest "$base/SHA256SUMS" -OutFile $sums
  $sumLine=Get-Content $sums|Where-Object{$_ -match [regex]::Escape($archiveName)}|Select-Object -First 1
  if(-not $sumLine){throw "Checksum entry not found for $archiveName"}
  $expected=($sumLine -split '\s+')[0].ToLowerInvariant()
  $actual=(Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant()
  if($actual -ne $expected){throw "Ubuntu VHD archive checksum mismatch expected=$expected actual=$actual"}

  # Windows bsdtar is prior-art tooling already on the runner. Extraction may materialize a
  # sparse fixed VHD; capacity census established sufficient D: headroom before this experiment.
  & tar.exe -xzf $archive -C $root
  if($LASTEXITCODE -ne 0){throw "tar extraction failed exit=$LASTEXITCODE"}
  $osVhd=Get-ChildItem $root -Filter '*.vhd'|Where-Object{$_.Name -ne 'cidata.vhd'}|Select-Object -First 1
  if(-not $osVhd){throw 'No Ubuntu VHD found after extraction'}

  # Tiny writable NoCloud seed. The guest must generate the nonce; the host never pre-creates
  # guest-nonce.txt, so observing a valid UUID later is an execution oracle rather than echo.
  New-VHD -Path $seed -Fixed -SizeBytes 96MB|Out-Null
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
runcmd:
  - |
    set -eu
    dev="$(blkid -L CIDATA)"
    mkdir -p /mnt/cidata-rw
    mount -o rw "$dev" /mnt/cidata-rw || mount -o remount,rw "$dev" /mnt/cidata-rw
    cat /proc/sys/kernel/random/uuid > /mnt/cidata-rw/guest-nonce.txt
    cat /proc/sys/kernel/random/boot_id > /mnt/cidata-rw/guest-boot-id.txt
    uname -a > /mnt/cidata-rw/guest-uname.txt
    sync
'@ | Set-Content -Encoding ascii (Join-Path $seedRoot 'user-data')
  Dismount-VHD -Path $seed

  $vm=New-VM -Name $name -Generation 2 -MemoryStartupBytes $memoryBytes -VHDPath $osVhd.FullName
  Set-VMProcessor -VMName $name -Count 1
  Set-VMFirmware -VMName $name -EnableSecureBoot Off
  Add-VMHardDiskDrive -VMName $name -ControllerType SCSI -Path $seed
  $switch=Get-VMSwitch|Where-Object{$_.Name -eq 'Default Switch'}|Select-Object -First 1
  if(-not $switch){$switch=Get-VMSwitch|Select-Object -First 1}
  if($switch){Connect-VMNetworkAdapter -VMName $name -SwitchName $switch.Name}

  Start-VM -Name $name
  $deadline=(Get-Date).AddSeconds($TimeoutSeconds)
  $heartbeatSeen=$false
  while((Get-Date) -lt $deadline){
    $hb=Get-VMIntegrationService -VMName $name -Name 'Heartbeat' -ErrorAction SilentlyContinue
    if($hb -and $hb.PrimaryStatusDescription -eq 'OK'){$heartbeatSeen=$true}
    Start-Sleep -Seconds 5
  }
  Stop-VM -Name $name -TurnOff -Force -ErrorAction SilentlyContinue
  Start-Sleep -Seconds 2

  $seedDisk=Mount-VHD -Path $seed -PassThru|Get-Disk
  $vol=$seedDisk|Get-Partition|Get-Volume|Where-Object FileSystemLabel -eq 'CIDATA'|Select-Object -First 1
  $guestNonce=$null;$bootId=$null;$uname=$null
  if($vol -and $vol.DriveLetter){
    $receiptRoot="$($vol.DriveLetter):\"
    $noncePath=Join-Path $receiptRoot 'guest-nonce.txt'
    if(Test-Path $noncePath){$guestNonce=(Get-Content $noncePath -Raw).Trim()}
    $bootPath=Join-Path $receiptRoot 'guest-boot-id.txt'
    if(Test-Path $bootPath){$bootId=(Get-Content $bootPath -Raw).Trim()}
    $unamePath=Join-Path $receiptRoot 'guest-uname.txt'
    if(Test-Path $unamePath){$uname=(Get-Content $unamePath -Raw).Trim()}
  }
  Dismount-VHD -Path $seed
  $validGuid=$false
  if($guestNonce){$tmp=[guid]::Empty;$validGuid=[guid]::TryParse($guestNonce,[ref]$tmp)}
  $ips=@((Get-VMNetworkAdapter -VMName $name -ErrorAction SilentlyContinue).IPAddresses|Where-Object{$_})

  Write-Receipt ([ordered]@{
    schema='hyperv-linux-l2-nonce/v2'; architecture=$arch; source='Canonical Ubuntu 24.04 Azure VHD';
    release=$release; archive=$archiveName; archiveSha256=$actual; vmCreated=$true;
    vmStarted=$true; heartbeatSeen=$heartbeatSeen; guestNonce=$guestNonce; guestBootId=$bootId;
    guestUname=$uname; observedGuestIPs=$ips; l2Executed=$validGuid; oracleSatisfied=$validGuid;
    classification=$(if($validGuid){'L2_EXECUTION_PROVEN'}else{'GUEST_BOOT_OR_CLOUD_INIT_NOT_PROVEN'});
    reason=$(if($validGuid){'Guest-generated UUID was written to the NoCloud seed disk and recovered by L1 after shutdown.'}else{'VM started but no valid guest-generated nonce was recovered within the bounded timeout.'})
  })
} catch {
  Write-Receipt ([ordered]@{
    schema='hyperv-linux-l2-nonce/v2'; architecture=$arch; vmCreated=($null-ne$vm);
    l2Executed=$false; oracleSatisfied=$false; classification='ENVIRONMENT_OR_HARNESS_FAILURE';
    reason=$_.Exception.Message
  })
} finally {
  if($vm){
    Stop-VM -Name $name -TurnOff -Force -ErrorAction SilentlyContinue
    Remove-VM -Name $name -Force -ErrorAction SilentlyContinue
  }
  Dismount-VHD -Path $seed -ErrorAction SilentlyContinue
  Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
}
