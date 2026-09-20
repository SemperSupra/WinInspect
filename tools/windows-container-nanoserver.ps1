param([int]$TimeoutSeconds=180)
$ErrorActionPreference='Stop'

$image='mcr.microsoft.com/windows/nanoserver:ltsc2025'
$out=Join-Path $env:RUNNER_TEMP 'windows-container-nanoserver.json'
$state=[ordered]@{
  schema='windows-container-nanoserver/v1'
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
  $noncePrefix=[guid]::NewGuid().ToString('N').Substring(0,12)
  $cmd='set /p H=<NUL & for /f "delims=" %i in (''hostname'') do @echo CONTAINER_HOST=%i & echo NONCE_PREFIX='+$noncePrefix+' & echo RANDOM_NONCE=%RANDOM%-%RANDOM% & ver'
  $sw=[Diagnostics.Stopwatch]::StartNew()
  $text=(& docker run --rm --isolation=$isolation $image cmd.exe /d /s /c $cmd 2>&1|Out-String).Trim()
  $code=$LASTEXITCODE
  $sw.Stop()
  [ordered]@{
    attempted=$true
    exitCode=$code
    seconds=[math]::Round($sw.Elapsed.TotalSeconds,3)
    output=$text
    oracleSatisfied=($code -eq 0 -and $text -match [regex]::Escape("NONCE_PREFIX=$noncePrefix") -and $text -match 'RANDOM_NONCE=\d+-\d+')
  }
}

try {
  $d=Get-Command docker -ErrorAction Stop
  $state.docker.command=$d.Source
  $state.docker.version=(& docker version --format '{{json .}}' 2>&1|Out-String).Trim()
  if($LASTEXITCODE -ne 0){throw 'Docker daemon is not callable'}

  $free=(Get-PSDrive C).Free
  $state.host.freeCBytes=[int64]$free
  if($free -lt 6GB){throw 'Less than 6 GiB free; refusing Windows container experiment'}

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

  # This is the useful nested-isolation oracle. Failure is recorded, not papered over.
  $state.hyperv=Run-Probe 'hyperv'
  $state.classification=if($state.hyperv.oracleSatisfied){
    'WINDOWS_NANOSERVER_HYPERV_ISOLATION_PROVEN'
  }elseif($state.process.oracleSatisfied){
    'WINDOWS_NANOSERVER_PROCESS_ISOLATION_ONLY'
  }else{
    'WINDOWS_CONTAINER_EXECUTION_UNPROVEN'
  }
} catch {
  $state.classification='ENVIRONMENT_OR_HARNESS_FAILURE'
  $state.error=$_.Exception.Message
} finally {
  Save-State
  Get-Content $out
}
