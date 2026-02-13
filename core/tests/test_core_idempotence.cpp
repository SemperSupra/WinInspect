#include "doctest/doctest.h"
#include "wininspect/core.hpp"
#include "wininspect/fake_backend.hpp"

using namespace wininspect;

DOCTEST_TEST_CASE("ensureVisible is idempotent on FakeBackend") {
  FakeBackend fb({
    {1,0,0,"A","C1",true},
    {2,0,0,"B","C2",false},
  });

  Snapshot s = fb.capture_snapshot();
  CoreEngine core(&fb);

  CoreRequest r1;
  r1.id = "1";
  r1.method = "window.ensureVisible";
  r1.params["hwnd"] = std::string("0x2");
  r1.params["visible"] = true;

  auto resp1 = core.handle(r1, s);
  DOCTEST_REQUIRE(resp1.ok);

  // Second call should report changed=false
  auto resp2 = core.handle(r1, s);
  DOCTEST_REQUIRE(resp2.ok);
  auto obj = resp2.result.as_obj();
  DOCTEST_REQUIRE(obj.at("changed").is_bool());
  DOCTEST_REQUIRE_EQ(obj.at("changed").as_bool(), false);
}
