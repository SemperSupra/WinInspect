$ErrorActionPreference = 'Stop'
$path = 'source/daemon/src/tcp_server.cpp'
$text = [IO.File]::ReadAllText($path).Replace("`r`n", "`n")
$old = @'
          th.t = std::thread([this, client, auth_keys, read_only, admin_logs, no_clipboard,
                              done = th.done, cfg]() {
'@.Replace("`r`n", "`n")
$new = @'
          th.t = std::thread([this, client, cfg, auth_keys, read_only, admin_logs, no_clipboard,
                              done = th.done]() {
'@.Replace("`r`n", "`n")
$first = $text.IndexOf($old, [StringComparison]::Ordinal)
if ($first -lt 0) { throw 'Expected projected TCP capture-order block not found.' }
if ($text.IndexOf($old, $first + $old.Length, [StringComparison]::Ordinal) -ge 0) { throw 'Projected TCP capture-order block is not unique.' }
$text = $text.Substring(0, $first) + $new + $text.Substring($first + $old.Length)
[IO.File]::WriteAllText($path, $text, [Text.UTF8Encoding]::new($false))
Write-Host 'Projected TCP capture order normalized without semantic change.'
