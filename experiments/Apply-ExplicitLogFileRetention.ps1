$ErrorActionPreference = 'Stop'

function Replace-Exactly {
    param([string]$Path,[string]$Old,[string]$New)
    $text = [IO.File]::ReadAllText($Path).Replace("`r`n", "`n")
    $oldNorm = $Old.Replace("`r`n", "`n")
    $newNorm = $New.Replace("`r`n", "`n")
    $first = $text.IndexOf($oldNorm,[StringComparison]::Ordinal)
    if ($first -lt 0) { throw "Expected log-file block not found in $Path" }
    if ($text.IndexOf($oldNorm,$first + $oldNorm.Length,[StringComparison]::Ordinal) -ge 0) { throw "Log-file block is not unique in $Path" }
    $updated = $text.Substring(0,$first) + $newNorm + $text.Substring($first + $oldNorm.Length)
    [IO.File]::WriteAllText($Path,$updated,[Text.UTF8Encoding]::new($false))
}

$path = 'source/daemon/src/server.cpp'

Replace-Exactly $path @'
  int max_event_log = 1000;
  int max_response_size = 64 * 1024 * 1024;
'@ @'
  int max_event_log = 1000;
  int max_response_size = 64 * 1024 * 1024;
  bool explicit_log_file = false;
'@

Replace-Exactly $path @'
    if (std::string(argv[i]) == "--log-file" && i + 1 < argc) {
      Logger::get().set_log_file(argv[++i]);
    }
'@ @'
    if (std::string(argv[i]) == "--log-file" && i + 1 < argc) {
      Logger::get().set_log_file(argv[++i]);
      explicit_log_file = true;
    }
'@

Replace-Exactly $path @'
  // Auto-create log directory in config dir (if --log-file not specified)
  Logger::get().set_log_dir(default_config_dir() + "/logs");
'@ @'
  // Auto-create log directory only when the user did not select an explicit file.
  if (!explicit_log_file)
    Logger::get().set_log_dir(default_config_dir() + "/logs");
'@

Write-Host 'Explicit --log-file retention candidate applied.'
