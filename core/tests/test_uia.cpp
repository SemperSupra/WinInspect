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

DOCTEST_TEST_CASE("UIA tree serialization works") {
  FakeBackend backend({});
  hwnd_u64 root = 0x100;
  
  UIElementInfo parent;
  parent.automation_id = "p";
  UIElementInfo child;
  child.automation_id = "c";
  UIElementInfo grandchild;
  grandchild.automation_id = "g";
  
  child.children.push_back(grandchild);
  parent.children.push_back(child);
  
  backend.add_fake_ui_element(root, parent);
  
  CoreEngine core(&backend);
  Snapshot s;
  CoreRequest req;
  req.id = "depth-test";
  req.method = "ui.inspect";
  req.params["hwnd"] = "0x100";
  
  CoreResponse resp = core.handle(req, s);
  DOCTEST_REQUIRE(resp.ok);
  
  auto res = resp.result.as_arr()[0].as_obj();
  DOCTEST_REQUIRE(res.at("children").as_arr().size() == 1);
  DOCTEST_REQUIRE(res.at("children").as_arr()[0].as_obj().at("children").as_arr().size() == 1);
}

DOCTEST_TEST_CASE("ui.invoke handles missing elements") {
  FakeBackend backend({});
  CoreEngine core(&backend);
  Snapshot s;
  
  CoreRequest req;
  req.id = "invoke-fail";
  req.method = "ui.invoke";
  req.params["hwnd"] = "0x999";
  req.params["automation_id"] = "non-existent";
  
  CoreResponse resp = core.handle(req, s);
  DOCTEST_REQUIRE(resp.ok);
  DOCTEST_REQUIRE(resp.result.as_obj().at("invoked").as_bool() == false);
}
