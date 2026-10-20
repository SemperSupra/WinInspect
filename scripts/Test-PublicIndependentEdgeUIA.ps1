#Requires -Version 7.0
# Independent read-only UIA oracle on a disposable synthetic Edge window.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$receipt = [ordered]@{schema='public-independent-edge-uia/v1';status='NOT_RUN';rootFound=$false;childCount=0;errorClass=$null;cleanup='NOT_RUN'}
$work=Join-Path $env:RUNNER_TEMP ('edge-uia-'+[Guid]::NewGuid().ToString('N'))
$profile=Join-Path $work 'profile'
$proc=$null
try {
    New-Item -Path $profile -ItemType Directory -Force | Out-Null
    $pf=[Environment]::GetFolderPath('ProgramFiles')
    $pfx=[Environment]::GetFolderPath('ProgramFilesX86')
    $binary=@((Join-Path $pfx 'Microsoft/Edge/Application/msedge.exe'),(Join-Path $pf 'Microsoft/Edge/Application/msedge.exe')) | Where-Object {Test-Path -LiteralPath $_ -PathType Leaf} | Select-Object -First 1
    if (-not $binary) {$receipt.status='EDGE_NOT_PRESENT'}
    else {
        $baseline=@(Get-Process -Name msedge -ErrorAction SilentlyContinue | ForEach-Object Id)
        $args=@("--user-data-dir=$profile",'--no-first-run','--no-default-browser-check','--new-window','data:text/html,%3Ctitle%3ESynthetic%3C/title%3E%3Cbutton%3EFixture%3C/button%3E')
        $proc=Start-Process -FilePath $binary -ArgumentList $args -PassThru
        $target=$null
        foreach($attempt in 1..30) {
            Start-Sleep -Milliseconds 300
            $new=@(Get-Process -Name msedge -ErrorAction SilentlyContinue | Where-Object {$_.Id -notin $baseline -and $_.MainWindowHandle -ne 0})
            if ($new.Count -gt 0) {$target=$new[0];break}
        }
        if (-not $target) {$receipt.status='WINDOW_NOT_VISIBLE'}
        else {
            try {
                Add-Type -AssemblyName UIAutomationClient -ErrorAction Stop
                $root=[System.Windows.Automation.AutomationElement]::FromHandle([IntPtr]$target.MainWindowHandle)
                if($null -eq $root){$receipt.status='UIA_ROOT_UNAVAILABLE'}
                else {
                    $receipt.rootFound=$true
                    $items=$root.FindAll([System.Windows.Automation.TreeScope]::Children,[System.Windows.Automation.Condition]::TrueCondition)
                    $receipt.childCount=$items.Count
                    $receipt.status=if ($items.Count -gt 0) {'CHILDREN_OBSERVED'} else {'ROOT_ONLY'}
                }
            } catch {$receipt.status='UIA_API_ERROR';$receipt.errorClass=$_.Exception.GetType().Name}
        }
    }
} catch {$receipt.status='SETUP_ERROR';$receipt.errorClass=$_.Exception.GetType().Name}
finally {
    if($null -ne $proc) {
        try {if(-not $proc.HasExited){$proc.Kill($true)};$receipt.cleanup='PROCESS_STOPPED'}
        catch {$receipt.cleanup='KILL_FAILED'}
    }
    if(Test-Path $work) {
        try {Remove-Item $work -Recurse -Force -ErrorAction Stop}
        catch {$receipt.cleanup='PROFILE_CLEANUP_FAILED'}
    }
    $out=Join-Path $env:RUNNER_TEMP 'edge-independent-uia.json'
    $receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $out -Encoding utf8NoBOM
    Write-Host "INDEPENDENT_EDGE_UIA=$($receipt.status) CHILDREN=$($receipt.childCount) CLEANUP=$($receipt.cleanup)"
}