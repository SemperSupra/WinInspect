$ErrorActionPreference = 'Stop'

function Replace-Exactly {
    param([string]$Path,[string]$Old,[string]$New)
    $text = [IO.File]::ReadAllText($Path).Replace("`r`n", "`n")
    $oldNorm = $Old.Replace("`r`n", "`n")
    $newNorm = $New.Replace("`r`n", "`n")
    $first = $text.IndexOf($oldNorm,[StringComparison]::Ordinal)
    if ($first -lt 0) { throw "Expected response-id block not found in $Path" }
    if ($text.IndexOf($oldNorm,$first + $oldNorm.Length,[StringComparison]::Ordinal) -ge 0) { throw "Response-id block is not unique in $Path" }
    $updated = $text.Substring(0,$first) + $newNorm + $text.Substring($first + $oldNorm.Length)
    [IO.File]::WriteAllText($Path,$updated,[Text.UTF8Encoding]::new($false))
}

$core = 'source/core/src/core.cpp'
Replace-Exactly $core @'
  after_dispatch:
    auto end_time = std::chrono::steady_clock::now();
'@ @'
  after_dispatch:
    // Every protocol response is correlated to the request even when a dispatch
    // handler constructs and returns a fresh CoreResponse.
    resp.id = req.id;
    auto end_time = std::chrono::steady_clock::now();
'@

New-Item -ItemType Directory -Force -Path source/tests | Out-Null
$test = @'
#include "wininspect/core.hpp"
#include "wininspect/fake_backend.hpp"
#include <iostream>

int main()
{
  using namespace wininspect;
  FakeBackend backend({});
  CoreEngine core(&backend);
  CoreRequest req;
  req.id = "correlation-sentinel";
  req.method = "daemon.version";
  req.params = json::Object{};
  auto snapshot = backend.capture_snapshot();
  auto resp = core.handle(req, snapshot, nullptr);
  if (!resp.ok) {
    std::cerr << "daemon.version unexpectedly failed\n";
    return 1;
  }
  if (resp.id != req.id) {
    std::cerr << "response id mismatch: expected '" << req.id << "', got '" << resp.id << "'\n";
    return 2;
  }
  return 0;
}
'@
[IO.File]::WriteAllText('source/tests/test_response_id.cpp',$test.Replace("`r`n","`n"),[Text.UTF8Encoding]::new($false))

$cmake = 'source/CMakeLists.txt'
Replace-Exactly $cmake @'
  add_executable(test_discovery
'@ @'
  add_executable(test_response_id
    tests/test_response_id.cpp
  )
  target_include_directories(test_response_id PRIVATE core/include third_party)
  target_link_libraries(test_response_id PRIVATE wininspect_core)
  add_test(NAME test_response_id COMMAND test_response_id WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  add_executable(test_discovery
'@

Write-Host 'RPC response-id correlation candidate applied.'
