$ErrorActionPreference = 'Stop'
$path = 'source/tests/test_daemon_shutdown_lifecycle.cpp'
$text = [IO.File]::ReadAllText($path).Replace("`r`n", "`n")
$old = '  FakeBackend backend;'
$new = '  FakeBackend backend({});'
$first = $text.IndexOf($old, [StringComparison]::Ordinal)
if ($first -lt 0) { throw 'Expected generated FakeBackend fixture block not found.' }
if ($text.IndexOf($old, $first + $old.Length, [StringComparison]::Ordinal) -ge 0) { throw 'Generated FakeBackend fixture block is not unique.' }
$text = $text.Substring(0, $first) + $new + $text.Substring($first + $old.Length)
[IO.File]::WriteAllText($path, $text, [Text.UTF8Encoding]::new($false))
Write-Host 'Generated V2-B1 FakeBackend fixture normalized to explicit empty window vector.'
