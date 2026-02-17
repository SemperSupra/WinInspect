#include "doctest/doctest.h"
#include "wininspect/core.hpp"
#include "wininspect/fake_backend.hpp"

using namespace wininspect;

DOCTEST_TEST_CASE("UI Inspection works") {
  FakeBackend backend({});

  // Setup fake UI elements
  UIElementInfo u1;
  u1.automation_id = "btn1";
  u1.name = "Button 1";
  u1.control_type = "50000"; // Button
  u1.bounding_rect = {10, 10, 50, 30};
  u1.enabled = true;
  u1.visible = true;

  backend.add_fake_ui_element(0x1234, u1);

  CoreEngine core(&backend);
  Snapshot s; // Empty snapshot fine for fake backend

  CoreRequest req;
  req.id = "1";
  req.method = "ui.inspect";
  json::Object params;
  params["hwnd"] = "0x1234";
  req.params = params;

  CoreResponse resp = core.handle(req, s);
  DOCTEST_REQUIRE(resp.ok);

  auto arr = resp.result.as_arr();
  DOCTEST_REQUIRE(arr.size() == 1);
  auto obj = arr[0].as_obj();
  DOCTEST_REQUIRE(obj["automation_id"].as_str() == "btn1");
  DOCTEST_REQUIRE(obj["name"].as_str() == "Button 1");
  DOCTEST_REQUIRE(obj["control_type"].as_str() == "50000");

  auto rect = obj["bounding_rect"].as_obj();
  DOCTEST_REQUIRE(rect["left"].as_num() == 10.0);
}
