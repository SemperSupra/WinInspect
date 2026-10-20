#Requires -Version 7.0
# Independent UIAutomationClient readback for precisely the WinInspect-observed HWND.
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][ValidatePattern('^0x[0-9A-Fa-f]{1,16}$')][string]$Hwnd,
  [Parameter(Mandatory=$true)][ValidateRange(1,2147483647)][int]$ExpectedPid
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$result=[ordered]@{ status='NOT_RUN'; rootFound=$false; childCount=0; pidMatched=$false; errorClass=$null }
try {
  $native = @'
using System;
using System.Runtime.InteropServices;
public static class WinInspectReadOnlyPid {
  [DllImport("user32.dll")]
  public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
}
'@
  Add-Type -TypeDefinition $native -ErrorAction Stop
  $number=[Convert]::ToUInt64($Hwnd.Substring(2),16)
  $window=[IntPtr]::new([long]$number)
  [uint32]$observedPid=0
  $thread=[WinInspectReadOnlyPid]::GetWindowThreadProcessId($window,[ref]$observedPid)
  $result.pidMatched=($thread -ne 0 -and $observedPid -eq [uint32]$ExpectedPid)
  if (-not $result.pidMatched) {$result.status='STALE_OR_CHANGED_WINDOW'}
  else {
    Add-Type -AssemblyName UIAutomationClient -ErrorAction Stop
    $root=[System.Windows.Automation.AutomationElement]::FromHandle($window)
    if ($null -eq $root) {$result.status='NO_UIA_ROOT'}
    else {
      $result.rootFound=$true
      $children=$root.FindAll([System.Windows.Automation.TreeScope]::Children,[System.Windows.Automation.Condition]::TrueCondition)
      $result.childCount=$children.Count
      $result.status=if($children.Count -gt 0){'CHILDREN_OBSERVED'}else{'EMPTY_CHILD_TREE'}
    }
  }
} catch {
  $result.status='INDEPENDENT_UIA_ERROR'
  $result.errorClass=$_.Exception.GetType().Name
}
$result | ConvertTo-Json -Compress -Depth 3
