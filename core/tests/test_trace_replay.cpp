#include "doctest/doctest.h"
#include "wininspect/core.hpp"
#include "wininspect/fake_backend.hpp"
#include <fstream>
#include <sstream>

using namespace wininspect;

static std::string read_file(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

DOCTEST_TEST_CASE("trace replay: two_clients_non_interference") {
  // Minimal replay: validate key expectations using fake backend
  auto trace = wininspect::json::parse(
                   read_file("formal/traces/two_clients_non_interference.json"))
                   .as_obj();
  auto windows = trace.at("initial_world").as_obj().at("windows").as_arr();

  std::vector<FakeWindow> fw;
  for (const auto &w : windows) {
    auto &o = w.as_obj();
    auto hwnd_s = o.at("hwnd").as_str();
    // parse as hex without 0x for simplicity
    std::uint64_t hwnd = std::stoull(hwnd_s.substr(2), nullptr, 16);
    fw.push_back({(hwnd_u64)hwnd, 0, 0, o.at("title").as_str(),
                  o.at("class").as_str(), o.at("visible").as_bool()});
  }
  FakeBackend fb(std::move(fw));
  Snapshot s = fb.capture_snapshot();
  CoreEngine core(&fb);

  // c2 ensureVisible twice -> second changed=false
  CoreRequest req;
  req.id = "c2-1";
  req.method = "window.ensureVisible";
  req.params["hwnd"] = std::string("0x2");
  req.params["visible"] = true;

  auto r1 = core.handle(req, s);
  DOCTEST_REQUIRE(r1.ok);

  req.id = "c2-2";
  auto r2 = core.handle(req, s);
  DOCTEST_REQUIRE(r2.ok);
  DOCTEST_REQUIRE_EQ(r2.result.as_obj().at("changed").as_bool(), false);
}

DOCTEST_TEST_CASE("trace replay: uia_recursive_tree") {
  auto trace = wininspect::json::parse(
                   read_file("formal/traces/uia_recursive_tree.json"))
                   .as_obj();
  
  FakeBackend fb({});
  // In a real replay we would populate the fake backend based on the trace 
  // 'initial_world' or specific step mocks. For now, we simulate the response 
  // to verify the core serializes correctly.
  
  UIElementInfo root;
  root.automation_id = "root_pane";
  root.name = "Root Pane";
  root.control_type = "50033";
  root.bounding_rect = {0, 0, 800, 600};
  root.enabled = true;
  root.visible = true;
  
  UIElementInfo child;
  child.automation_id = "submit_btn";
  child.name = "Submit";
  child.control_type = "50000";
  child.bounding_rect = {100, 100, 200, 130};
  child.enabled = true;
  child.visible = true;
  
  root.children.push_back(child);
  fb.add_fake_ui_element(0x1001, root);
  
  CoreEngine core(&fb);
  Snapshot s;
  CoreRequest req;
  req.id = "trace-uia-1";
  req.method = "ui.inspect";
  req.params["hwnd"] = "0x1001";
  
  CoreResponse resp = core.handle(req, s);
  DOCTEST_REQUIRE(resp.ok);
  
  auto res = resp.result.as_arr()[0].as_obj();
  DOCTEST_REQUIRE(res.at("automation_id").as_str() == "root_pane");
  DOCTEST_REQUIRE(res.at("children").as_arr().size() == 1);
  DOCTEST_REQUIRE(res.at("children").as_arr()[0].as_obj().at("automation_id").as_str() == "submit_btn");
}

DOCTEST_TEST_CASE("trace replay: system_orchestration") {
  FakeBackend fb({});
  CoreEngine core(&fb);
  Snapshot s;

  // Process List
  CoreRequest req1{"t-1", "process.list", {}};
  auto r1 = core.handle(req1, s);
  DOCTEST_REQUIRE(r1.ok);
  DOCTEST_REQUIRE(r1.result.as_arr().size() == 1);
  DOCTEST_REQUIRE(r1.result.as_arr()[0].as_obj().at("name").as_str() == "fake.exe");

  // Registry Read
  json::Object p2; p2["path"] = std::string("HKCU\\Software\\Test");
  CoreRequest req2{"t-2", "reg.read", p2};
  auto r2 = core.handle(req2, s);
  DOCTEST_REQUIRE(r2.ok);
  DOCTEST_REQUIRE(r2.result.as_obj().at("values").as_arr().size() == 1);

  // Clipboard
  json::Object p3; p3["text"] = std::string("TraceData");
  CoreRequest req3{"t-3", "clipboard.write", p3};
  auto r3 = core.handle(req3, s);
  DOCTEST_REQUIRE(r3.ok);

  CoreRequest req4{"t-4", "clipboard.read", {}};
  auto r4 = core.handle(req4, s);
  DOCTEST_REQUIRE(r4.ok);
  DOCTEST_REQUIRE(r4.result.as_obj().at("text").as_str() == "fake clipboard");
}
