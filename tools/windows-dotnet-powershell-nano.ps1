param([int]$TimeoutSeconds=180)
$ErrorActionPreference='Stop'

$image='mcr.microsoft.com/dotnet/sdk:10.0-nanoserver-ltsc2025'
$out=Join-Path $env:RUNNER_TEMP 'windows-dotnet-powershell-nano.json'
$state=[ordered]@{
  schema='windows-dotnet-powershell-nano/v1'
  timestamp=(Get-Date).ToUniversalTime().ToString('o')
  image=$image
  host=[ordered]@{
    os=[Environment]::OSVersion.VersionString
    arch=[Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
  }
  docker=[ordered]@{}
  process=[ordered]@{}
  hyperv=[ordered]@{}
  classification='IN_PROGRESS'
}

function Save-State {$state|ConvertTo-Json -Depth 8|Set-Content -Encoding UTF8 $out}
function Run-Probe([string]$isolation){
  $prefix=[guid]::NewGuid().ToString('N').Substring(0,12)
  $command = '$g=[guid]::NewGuid().ToString(); Write-Output ("CONTAINER_HOST="+$env:COMPUTERNAME); Write-Output ("NONCE_PREFIX='+$prefix+'"); Write-Output ("GUID_NONCE="+$g); Write-Output ("PS_VERSION="+$PSVersionTable.PSVersion.ToString()); Write-Output ("DOTNET_VERSION="+(& dotnet --version))'
  $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
  $sw=[Diagnostics.Stopwatch]::StartNew()
  $text=(& docker run --rm --isolation=$isolation $image pwsh -NoLogo -NoProfile -NonInteractive -EncodedCommand $encoded 2>&1|Out-String).Trim()
  $code=$LASTEXITCODE
  $sw.Stop()
  [ordered]@{
    attempted=$true
    exitCode=$code
    seconds=[math]::Round($sw.Elapsed.TotalSeconds,3)
    output=$text
    oracleSatisfied=($code -eq 0 -and $text -match [regex]::Escape("NONCE_PREFIX=$prefix") -and $text -match 'GUID_NONCE=[0-9a-fA-F-]{36}' -and $text -match 'PS_VERSION=' -and $text -match 'DOTNET_VERSION=')
  }
}

try {
  $d=Get-Command docker -ErrorAction Stop
  $state.docker.command=$d.Source
  $svc=Get-Service docker -ErrorAction SilentlyContinue
  $state.docker.serviceInitial=if($svc){$svc.Status.ToString()}else{'ABSENT'}
  $versionText=(& docker version --format '{{json .}}' 2>&1|Out-String).Trim()
  $versionCode=$LASTEXITCODE
  if($versionCode -ne 0 -and $svc){
    if($svc.Status -ne 'Running'){Start-Service docker -ErrorAction Stop}
    $deadline=(Get-Date).AddSeconds(20)
    do {
      Start-Sleep -Seconds 1
      $versionText=(& docker version --format '{{json .}}' 2>&1|Out-String).Trim()
      $versionCode=$LASTEXITCODE
    } while($versionCode -ne 0 -and (Get-Date) -lt $deadline)
  }
  $svc=Get-Service docker -ErrorAction SilentlyContinue
  $state.docker.serviceFinal=if($svc){$svc.Status.ToString()}else{'ABSENT'}
  $state.docker.version=$versionText
  if($versionCode -ne 0){throw "Docker daemon is not callable after bounded service recovery: $versionText"}

  $free=(Get-PSDrive C).Free
  $state.host.freeCBytes=[int64]$free
  if($free -lt 8GB){throw 'Less than 8 GiB free; refusing .NET/PowerShell Nano experiment'}

  $sw=[Diagnostics.Stopwatch]::StartNew()
  & docker pull $image | Out-Host
  if($LASTEXITCODE -ne 0){throw "docker pull failed: $image"}
  $sw.Stop()
  $state.docker.pullSeconds=[math]::Round($sw.Elapsed.TotalSeconds,3)

  $inspect=(& docker image inspect $image 2>&1|Out-String)
  if($LASTEXITCODE -eq 0){
    $obj=$inspect|ConvertFrom-Json|Select-Object -First 1
    $state.docker.imageId=$obj.Id
    $state.docker.repoDigests=@($obj.RepoDigests)
    $state.docker.sizeBytes=[int64]$obj.Size
    $state.docker.os=$obj.Os
    $state.docker.architecture=$obj.Architecture
  }
  Save-State

  $state.process=Run-Probe 'process'
  Save-State
  $state.hyperv=Run-Probe 'hyperv'

  $state.classification=if($state.hyperv.oracleSatisfied){
    'WINDOWS_NANO_DOTNET_POWERSHELL_HYPERV_ISOLATION_PROVEN'
  }elseif($state.process.oracleSatisfied){
    'WINDOWS_NANO_DOTNET_POWERSHELL_PROCESS_ISOLATION_ONLY'
  }else{
    'WINDOWS_NANO_DOTNET_POWERSHELL_EXECUTION_UNPROVEN'
  }
} catch {
  $state.classification='ENVIRONMENT_OR_HARNESS_FAILURE'
  $state.error=$_.Exception.Message
} finally {
  Save-State
  Get-Content $out
}
