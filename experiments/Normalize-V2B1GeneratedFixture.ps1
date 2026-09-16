$ErrorActionPreference = 'Stop'

function Replace-Exactly {
    param([string]$Path,[string]$Old,[string]$New)
    $text = [IO.File]::ReadAllText($Path).Replace("`r`n", "`n")
    $oldNorm = $Old.Replace("`r`n", "`n")
    $newNorm = $New.Replace("`r`n", "`n")
    $first = $text.IndexOf($oldNorm, [StringComparison]::Ordinal)
    if ($first -lt 0) { throw "Expected generated fixture block not found in $Path" }
    if ($text.IndexOf($oldNorm, $first + $oldNorm.Length, [StringComparison]::Ordinal) -ge 0) { throw "Generated fixture block is not unique in $Path" }
    $updated = $text.Substring(0,$first) + $newNorm + $text.Substring($first + $oldNorm.Length)
    [IO.File]::WriteAllText($Path,$updated,[Text.UTF8Encoding]::new($false))
}

Replace-Exactly 'source/tests/test_daemon_shutdown_lifecycle.cpp' '  FakeBackend backend;' '  FakeBackend backend({});'

Replace-Exactly 'source/CMakeLists.txt' @'
  add_executable(test_daemon_shutdown_lifecycle
    tests/test_daemon_shutdown_lifecycle.cpp
  )
'@ @'
  add_executable(test_daemon_shutdown_lifecycle
    tests/test_daemon_shutdown_lifecycle.cpp
    daemon/src/control_manager.cpp
  )
'@

Write-Host 'Generated V2-B1 focused fixture normalized to current FakeBackend and ControlManager linkage contracts.'
