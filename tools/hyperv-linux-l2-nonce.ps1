param([int]$MemoryMB=768,[int]$HeartbeatTimeoutSeconds=75,[int]$GuestSettleSeconds=20)
$ErrorActionPreference='Stop'
$name='l2-'+[guid]::NewGuid().ToString('N').Substring(0,8)
$hostArch=[Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
$arch=if($hostArch -eq 'Arm64'){'arm64'}else{'amd64'}
$receiptPath=Join-Path $env:RUNNER_TEMP "hyperv-linux-l2-$arch.json"
$state=[ordered]@{
  schema='hyperv-linux-l2-nonce/v4'
  architecture=$arch
  vmName=$name
  stage='init'
  stageTimestamp=(Get-Date).ToUniversalTime().ToString('o')
  l2Executed=$false
  oracleSatisfied=$false
  classification='IN_PROGRESS'
}

function Save-State {
  $state | ConvertTo-Json -Depth 10 | Set-Content -Encoding UTF8 $receiptPath
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
function Convert-IPv4ToUInt([string]$ip) {
  # Windows PowerShell 5.1 can coerce bit-shifts through signed Int32. Use arithmetic
  # so addresses with the high bit set (for example 172.x) cannot become negative.
  $b=[Net.IPAddress]::Parse($ip).GetAddressBytes()
  return ([uint64]$b[0] * 16777216 + [uint64]$b[1] * 65536 + [uint64]$b[2] * 256 + [uint64]$b[3])
}
function Convert-UIntToIPv4([uint64]$n) {
  $a=[uint64][math]::Floor($n / 16777216) % 256
  $b=[uint64][math]::Floor($n / 65536) % 256
  $c=[uint64][math]::Floor($n / 256) % 256
  $d=$n % 256
  return "$a.$b.$c.$d"
}
function Get-MaskFromPrefix([int]$prefix) {
  if($prefix -le 0){return '0.0.0.0'}
  if($prefix -ge 32){return '255.255.255.255'}
  $octets=New-Object int[] 4
  $full=[int][math]::Floor($prefix/8)
  $rem=$prefix % 8
  for($i=0;$i-lt4;$i++){
    if($i -lt $full){$octets[$i]=255}
    elseif($i -eq $full -and $rem -gt 0){$octets[$i]=[int](256-[math]::Pow(2,8-$rem))}
    else{$octets[$i]=0}
  }
  return ($octets -join '.')
}
function Test-TcpQuick([string]$ip,[int]$port,[int]$timeoutMs=2500) {
  $client=New-Object Net.Sockets.TcpClient
  try {
    $iar=$client.BeginConnect($ip,$port,$null,$null)
    if(-not $iar.AsyncWaitHandle.WaitOne($timeoutMs,$false)){return $false}
    $client.EndConnect($iar)
    return $true
  } catch { return $false } finally { $client.Close() }
}
function Get-GuestKvp($vmWmi) {
  $result=[ordered]@{}
  try {
    $component=@($vmWmi.GetRelated('Msvm_KvpExchangeComponent'))|Select-Object -First 1
    foreach($item in @($component.GuestIntrinsicExchangeItems)){
      try {
        [xml]$doc=$item
        $props=[ordered]@{}
        foreach($p in @($doc.INSTANCE.PROPERTY)){
          $props[[string]$p.NAME]=[string]$p.VALUE
        }
        if($props.Name){$result[[string]$props.Name]=[string]$props.Data}
      } catch {}
    }
  } catch {}
  return $result
}

Save-State

# Qualify x64 first; ARM64 runs only after the architecture-neutral oracle has proven itself.
if($arch -eq 'arm64'){
  $state.classification='DEFERRED_UNTIL_X64_NETWORK_ORACLE'
  $state.reason='ARM64 intentionally gated until the x64 L2 execution + host/guest network characterization path is qualified.'
  Set-Stage 'deferred-arm64'
  exit 0
}

$workDrive=Get-PSDrive -Name D -ErrorAction SilentlyContinue
if($workDrive -and $workDrive.Free -gt 8GB){$root="D:\$name"}else{$root=Join-Path $env:RUNNER_TEMP $name}
New-Item -ItemType Directory -Path $root -Force|Out-Null

$release='3.24.1'
$base='https://dl-cdn.alpinelinux.org/alpine/v3.24/releases/cloud'
$imageName="generic_alpine-$release-x86_64-uefi-cloudinit-r0.qcow2"
$image=Join-Path $root $imageName
$imageVhdx=Join-Path $root ([IO.Path]::GetFileNameWithoutExtension($imageName)+'.vhdx')
$sumFile="$image.sha512"
$seed=Join-Path $root 'cidata.vhdx'
$memoryBytes=[int64]$MemoryMB * 1MB
$vm=$null
$createdSwitchName=$null
$createdNatName=$null

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
  $state.source='Alpine Linux official generic UEFI cloud-init QCOW2 (NoCloud-capable)'
  $state.release=$release
  $state.image=$imageName
  $state.imageSha512=$actual
  Save-State

  Set-Stage 'convert-vhdx'
  $qemuImg=$null
  $cmd=Get-Command qemu-img.exe -ErrorAction SilentlyContinue
  if($cmd){$qemuImg=$cmd.Source}
  if(-not $qemuImg -and $env:ANDROID_HOME){
    $androidQemu=Join-Path $env:ANDROID_HOME 'emulator\qemu-img.exe'
    if(Test-Path -LiteralPath $androidQemu){$qemuImg=$androidQemu}
  }
  if(-not $qemuImg){throw 'Generic Alpine QCOW2 selected but no existing qemu-img primitive was found (PATH or Android Emulator).'}
  $state.qemuImg=$qemuImg
  $convertSw=[Diagnostics.Stopwatch]::StartNew()
  & $qemuImg convert -f qcow2 -O vhdx $image $imageVhdx
  if($LASTEXITCODE -ne 0){throw "qemu-img conversion failed with exit code $LASTEXITCODE"}
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

  Set-Stage 'characterize-host-network'
  # Use a disposable, controlled Hyper-V internal switch/NAT instead of depending on
  # an ambient Docker/Default Switch that varies between hosted-runner allocations.
  $switchName="$name-net"
  $natName="$name-nat"
  $selectedThirdOctet=$null
  $existingIps=@(Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue|ForEach-Object{$_.IPAddress})
  $existingPrefixes=@(Get-NetNat -ErrorAction SilentlyContinue|ForEach-Object{[string]$_.InternalIPInterfaceAddressPrefix})
  foreach($octet in @(252,253,254,251,250)){
    $candidatePrefix="192.168.$octet.0/24"
    $candidateStem="192.168.$octet."
    if(-not ($existingPrefixes -contains $candidatePrefix) -and -not @($existingIps|Where-Object{$_ -like "$candidateStem*"}).Count){
      $selectedThirdOctet=$octet
      break
    }
  }
  if($null -eq $selectedThirdOctet){throw 'No free bounded RFC1918 /24 candidate found for disposable L2 NAT'}
  $switch=New-VMSwitch -Name $switchName -SwitchType Internal -ErrorAction Stop
  $createdSwitchName=$switchName
  $ifAlias="vEthernet ($switchName)"
  $adapterDeadline=(Get-Date).AddSeconds(10)
  while((Get-Date) -lt $adapterDeadline -and -not (Get-NetAdapter -Name $ifAlias -ErrorAction SilentlyContinue)){Start-Sleep -Milliseconds 500}
  if(-not (Get-NetAdapter -Name $ifAlias -ErrorAction SilentlyContinue)){throw "Internal-switch host adapter did not appear: $ifAlias"}
  $hostAddress="192.168.$selectedThirdOctet.1"
  $prefix="192.168.$selectedThirdOctet.0/24"
  New-NetIPAddress -InterfaceAlias $ifAlias -IPAddress $hostAddress -PrefixLength 24 -ErrorAction Stop|Out-Null
  New-NetNat -Name $natName -InternalIPInterfaceAddressPrefix $prefix -ErrorAction Stop|Out-Null
  $createdNatName=$natName
  $hostIp=Get-NetIPAddress -InterfaceAlias $ifAlias -AddressFamily IPv4 -ErrorAction Stop|Where-Object{$_.IPAddress -eq $hostAddress}|Select-Object -First 1
  $state.networkSwitch=[ordered]@{name=$switch.Name;type=$switch.SwitchType.ToString();id=$switch.Id.ToString();createdForProbe=$true}
  $state.hostNats=@(Get-NetNat -ErrorAction SilentlyContinue|ForEach-Object{[ordered]@{name=$_.Name;prefix=$_.InternalIPInterfaceAddressPrefix;active=$_.Active}})
  $state.switchHostIPv4=$hostIp.IPAddress
  $state.switchHostPrefixLength=$hostIp.PrefixLength
  Save-State

  Set-Stage 'create-vm'
  $vm=New-VM -Name $name -Generation 2 -MemoryStartupBytes $memoryBytes -VHDPath $imageVhdx
  Set-VMProcessor -VMName $name -Count 1
  Set-VMFirmware -VMName $name -EnableSecureBoot Off
  $osDisk=Get-VMHardDiskDrive -VMName $name|Where-Object Path -eq $imageVhdx|Select-Object -First 1
  if($osDisk){Set-VMFirmware -VMName $name -FirstBootDevice $osDisk}
  Connect-VMNetworkAdapter -VMName $name -SwitchName $switch.Name

  # Configure the NoCloud seed with the exact Hyper-V synthetic NIC identity.
  if($hostIp){
    $prefixLength=[int]$hostIp.PrefixLength
    $hostNum=Convert-IPv4ToUInt $hostIp.IPAddress
    $guestNum=$hostNum+10
    $guestIp=Convert-UIntToIPv4 $guestNum
    $mask=Get-MaskFromPrefix $prefixLength
    $macRaw=(Get-VMNetworkAdapter -VMName $name|Select-Object -First 1).MacAddress
    $mac=($macRaw -replace '(.{2})(?!$)','$1:').ToLowerInvariant()
    $state.networkCandidate=[ordered]@{guestIPv4=$guestIp;subnetMask=$mask;gateway=$hostIp.IPAddress;prefixLength=$prefixLength;dns='1.1.1.1';mac=$mac}
    $seedDisk=Mount-VHD -Path $seed -PassThru|Get-Disk
    $seedVol=$seedDisk|Get-Partition|Get-Volume|Where-Object FileSystemLabel -eq 'CIDATA'|Select-Object -First 1
    if(-not $seedVol -or -not $seedVol.DriveLetter){throw 'CIDATA seed volume could not be remounted for NoCloud network configuration'}
    $seedRoot="$($seedVol.DriveLetter):\"
    @"
version: 1
config:
  - type: physical
    name: eth0
    mac_address: '$mac'
    subnets:
      - type: static
        address: $guestIp
        netmask: $mask
        gateway: $($hostIp.IPAddress)
        dns_nameservers:
          - 1.1.1.1
"@ | Set-Content -Encoding ascii (Join-Path $seedRoot 'network-config')
    Dismount-VHD -Path $seed
  }
  Add-VMHardDiskDrive -VMName $name -ControllerType SCSI -Path $seed
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
  $state.integrationServices=@(Get-VMIntegrationService -VMName $name -ErrorAction SilentlyContinue|ForEach-Object{[ordered]@{name=$_.Name;enabled=$_.Enabled;primaryStatus=$_.PrimaryStatusDescription;secondaryStatus=$_.SecondaryStatusDescription}})
  $vmWmi=Get-WmiObject -Namespace 'root\virtualization\v2' -Class 'Msvm_ComputerSystem'|Where-Object{$_.ElementName -eq $name}|Select-Object -First 1
  if($vmWmi){$state.guestKvpBeforeNetwork=Get-GuestKvp $vmWmi}
  $state.observedGuestIPsBeforeInjection=@((Get-VMNetworkAdapter -VMName $name -ErrorAction SilentlyContinue).IPAddresses|Where-Object{$_})
  $existingIPv4=@($state.observedGuestIPsBeforeInjection|Where-Object{$_ -match '^\d+\.\d+\.\d+\.\d+$' -and $_ -notlike '169.254.*'})|Select-Object -First 1
  if($existingIPv4){
    $state.preInjectionGuestIPv4=$existingIPv4
    $state.preInjectionL1ToL2Ping=Test-Connection -ComputerName $existingIPv4 -Count 1 -Quiet -ErrorAction SilentlyContinue
    $state.preInjectionL1ToL2Tcp22=Test-TcpQuick $existingIPv4 22
  }
  Save-State

  # Generic NoCloud is the primary network configuration path. KVP injection is a
  # fallback diagnostic only if no static NoCloud candidate could be prepared.
  if($heartbeatSeen -and $vmWmi -and $hostIp -and -not $state.networkCandidate){
    Set-Stage 'inject-guest-network'
    try {
      $nat=@(Get-NetNat -ErrorAction SilentlyContinue|Where-Object{$_.Name -eq $switch.Name})|Select-Object -First 1
      if(-not $nat){$nat=@(Get-NetNat -ErrorAction SilentlyContinue)|Select-Object -First 1}
      $prefixLength=if($nat -and $nat.InternalIPInterfaceAddressPrefix){[int](($nat.InternalIPInterfaceAddressPrefix -split '/')[1])}else{[int]$hostIp.PrefixLength}
      $hostNum=Convert-IPv4ToUInt $hostIp.IPAddress
      $guestNum=$hostNum+10
      $guestIp=Convert-UIntToIPv4 $guestNum
      $mask=Get-MaskFromPrefix $prefixLength
      $state.networkCandidate=[ordered]@{guestIPv4=$guestIp;subnetMask=$mask;gateway=$hostIp.IPAddress;prefixLength=$prefixLength;dns='1.1.1.1'}

      $vmSettings=@($vmWmi.GetRelated('Msvm_VirtualSystemSettingData'))|Where-Object{$_.VirtualSystemType -eq 'Microsoft:Hyper-V:System:Realized'}|Select-Object -First 1
      $vmNet=@($vmSettings.GetRelated('Msvm_SyntheticEthernetPortSettingData'))|Select-Object -First 1
      $netCfg=@($vmNet.GetRelated('Msvm_GuestNetworkAdapterConfiguration'))|Select-Object -First 1
      if(-not $netCfg){throw 'Guest network configuration object not exposed by KVP integration service'}
      $netCfg.IPAddresses=@($guestIp)
      $netCfg.Subnets=@($mask)
      $netCfg.DefaultGateways=@($hostIp.IPAddress)
      $netCfg.DNSServers=@('1.1.1.1')
      $netCfg.DHCPEnabled=$false
      $netCfg.ProtocolIFType=4096
      $svc=Get-WmiObject -Namespace 'root\virtualization\v2' -Class 'Msvm_VirtualSystemManagementService'
      $setResult=$svc.SetGuestNetworkAdapterConfiguration($vmWmi,@($netCfg.GetText(1)))
      $state.networkInjectionReturnValue=[int]$setResult.ReturnValue
      $injectOk=$false
      if($setResult.ReturnValue -eq 0){$injectOk=$true}
      elseif($setResult.ReturnValue -eq 4096){
        $job=[WMI]$setResult.Job
        $jobDeadline=(Get-Date).AddSeconds(20)
        while((Get-Date) -lt $jobDeadline -and ($job.JobState -eq 3 -or $job.JobState -eq 4)){
          Start-Sleep -Seconds 1
          $job=[WMI]$setResult.Job
        }
        $state.networkInjectionJobState=[int]$job.JobState
        $injectOk=($job.JobState -eq 7)
      }
      $state.networkInjectionSucceeded=$injectOk
      Start-Sleep -Seconds 6
      $state.observedGuestIPs=@((Get-VMNetworkAdapter -VMName $name -ErrorAction SilentlyContinue).IPAddresses|Where-Object{$_})
      $state.guestKvpAfterNetwork=Get-GuestKvp $vmWmi
      $state.l1ToL2Ping=Test-Connection -ComputerName $guestIp -Count 1 -Quiet -ErrorAction SilentlyContinue
      $state.l1ToL2Tcp22=Test-TcpQuick $guestIp 22
      $state.natSessionsFromGuest=@(Get-NetNatSession -ErrorAction SilentlyContinue|Where-Object{$_.InternalSourceAddress -eq $guestIp}|Select-Object -First 20|ForEach-Object{[ordered]@{internalSourceAddress=$_.InternalSourceAddress;internalSourcePort=$_.InternalSourcePort;remoteExternalDestinationAddress=$_.RemoteExternalDestinationAddress;remoteExternalDestinationPort=$_.RemoteExternalDestinationPort;protocol=$_.Protocol}})
      Save-State
    } catch {
      $state.networkInjectionSucceeded=$false
      $state.networkInjectionError=$_.Exception.Message
      Save-State
    }
  }

  if($heartbeatSeen -and $GuestSettleSeconds -gt 0){
    Set-Stage 'guest-settle'
    Start-Sleep -Seconds $GuestSettleSeconds
  }

  if($heartbeatSeen -and $state.networkCandidate){
    $candidateIp=[string]$state.networkCandidate.guestIPv4
    $state.observedGuestIPs=@((Get-VMNetworkAdapter -VMName $name -ErrorAction SilentlyContinue).IPAddresses|Where-Object{$_})
    $state.l1ToL2Ping=Test-Connection -ComputerName $candidateIp -Count 1 -Quiet -ErrorAction SilentlyContinue
    $state.l1ToL2Tcp22=Test-TcpQuick $candidateIp 22
    $state.natSessionsFromGuest=@(Get-NetNatSession -ErrorAction SilentlyContinue|Where-Object{$_.InternalSourceAddress -eq $candidateIp}|Select-Object -First 20|ForEach-Object{[ordered]@{internalSourceAddress=$_.InternalSourceAddress;internalSourcePort=$_.InternalSourcePort;remoteExternalDestinationAddress=$_.RemoteExternalDestinationAddress;remoteExternalDestinationPort=$_.RemoteExternalDestinationPort;protocol=$_.Protocol}})
    if($vmWmi){$state.guestKvpAfterNetwork=Get-GuestKvp $vmWmi}
    Save-State
  }

  Set-Stage 'stop-vm'
  Stop-VM -Name $name -TurnOff -Force -ErrorAction SilentlyContinue
  Start-Sleep -Seconds 2

  Set-Stage 'read-guest-receipt'
  $seedDisk=Mount-VHD -Path $seed -PassThru|Get-Disk
  $vol=$seedDisk|Get-Partition|Get-Volume|Where-Object FileSystemLabel -eq 'CIDATA'|Select-Object -First 1
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
  $state.l2Executed=([bool]$heartbeatSeen -or $validGuid)
  $state.oracleSatisfied=$state.l2Executed
  $networkProven=([bool]$state.preInjectionL1ToL2Ping -or [bool]$state.preInjectionL1ToL2Tcp22 -or [bool]$state.l1ToL2Ping -or [bool]$state.l1ToL2Tcp22)
  $state.l1L2NetworkProven=$networkProven

  if($validGuid -and $networkProven){
    $state.classification='L2_EXECUTION_AND_L1_L2_NETWORK_PROVEN'
    $state.reason='Guest execution, guest-generated seed nonce, and host/guest network reachability were all observed.'
  } elseif($networkProven){
    $state.classification='L2_EXECUTION_AND_L1_L2_NETWORK_PROVEN'
    $state.reason='Healthy Hyper-V heartbeat proves L2 execution; Microsoft KVP network injection plus an observed guest IP/reachability proves L1-L2 networking. NoCloud write-back remains unproven.'
  } elseif($heartbeatSeen){
    $state.classification='L2_EXECUTION_PROVEN_NETWORK_UNPROVEN'
    $state.reason='Healthy Hyper-V heartbeat proves L2 execution; guest-network injection/reachability did not satisfy an oracle.'
  } else {
    $state.classification='GUEST_BOOT_NOT_PROVEN'
    $state.reason='The VM control plane started the VM but no healthy Hyper-V heartbeat or guest nonce was observed.'
  }
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
  if($createdNatName){Remove-NetNat -Name $createdNatName -Confirm:$false -ErrorAction SilentlyContinue}
  if($createdSwitchName){Remove-VMSwitch -Name $createdSwitchName -Force -ErrorAction SilentlyContinue}
  Save-State
  Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
}
