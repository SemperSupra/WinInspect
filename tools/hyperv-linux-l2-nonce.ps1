param([int]$MemoryMB=768,[int]$TimeoutSeconds=90)
$ErrorActionPreference='Stop'
$name='l2-'+[guid]::NewGuid().ToString('N').Substring(0,8)
$root=Join-Path $env:RUNNER_TEMP $name
New-Item -ItemType Directory -Path $root|Out-Null
# Prior-art-first: use Alpine netboot kernel/initramfs, no custom guest image or installed OS.
$arch=if([Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq 'Arm64'){'aarch64'}else{'x86_64'}
$base="https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/$arch/netboot"
$kernel=Join-Path $root 'vmlinuz-lts';$initrd=Join-Path $root 'initramfs-lts'
Invoke-WebRequest "$base/vmlinuz-lts" -OutFile $kernel
Invoke-WebRequest "$base/initramfs-lts" -OutFile $initrd
$nonce=[guid]::NewGuid().ToString()
$memoryBytes=[int64]$MemoryMB * 1MB
# Hyper-V Gen2 cannot directly boot a Linux kernel/initrd via PowerShell. This probe therefore
# records whether the supported control plane can assemble the bounded VM without manufacturing
# a custom bootloader. A bootable EFI prior-art artifact is the next gate.
$vm=$null
try {
  $vm=New-VM -Name $name -Generation 2 -MemoryStartupBytes $memoryBytes -NoVHD
  Set-VMProcessor -VMName $name -Count 1
  $switch=Get-VMSwitch|Where-Object SwitchType -eq 'Internal'|Select-Object -First 1
  if(-not $switch){$switch=New-VMSwitch -Name "$name-nat" -SwitchType Internal}
  Connect-VMNetworkAdapter -VMName $name -SwitchName $switch.Name
  $receipt=[ordered]@{schema='hyperv-linux-l2-nonce/v1';architecture=$arch;vmCreated=$true;kernelDownloaded=(Test-Path $kernel);initrdDownloaded=(Test-Path $initrd);networkSwitch=$switch.Name;nonce=$nonce;l2Executed=$false;oracleSatisfied=$false;classification='BOOT_PATH_REQUIRED';reason='Gen2 Hyper-V firmware requires a bootable EFI artifact; deliberately stopped rather than introduce a custom bootloader. Next use established bootable Alpine/cloud image prior art.'}
} catch {
  $receipt=[ordered]@{schema='hyperv-linux-l2-nonce/v1';architecture=$arch;vmCreated=($null-ne$vm);nonce=$nonce;l2Executed=$false;oracleSatisfied=$false;classification='ENVIRONMENT_FAILURE';reason=$_.Exception.Message}
} finally {
  if($vm){Remove-VM -Name $name -Force -ErrorAction SilentlyContinue}
  Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
}
$out=Join-Path $env:RUNNER_TEMP "hyperv-linux-l2-$arch.json";$receipt|ConvertTo-Json -Depth 5|Set-Content -Encoding UTF8 $out;$receipt|ConvertTo-Json -Depth 5
