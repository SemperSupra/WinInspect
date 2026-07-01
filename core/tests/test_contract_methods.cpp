// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#include "doctest/doctest.h"
#include "wininspect/core.hpp"
#include "wininspect/fake_backend.hpp"
#include "wininspect/base64.hpp"

using namespace wininspect;

// All tests use FakeBackend — deterministic, no OS dependency.
// Coverage: every protocol method gets at least one valid-request test.

static FakeBackend make_fake() {
  return FakeBackend({
      {1, 0, 0, "Window A", "ClassA", true},
      {2, 1, 0, "Child B",  "ClassB", true},
      {3, 0, 0, "Window C", "ClassC", false},
  });
}

// --- window methods ---

DOCTEST_TEST_CASE("contract: window.listTop") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  CoreRequest req{"t1","window.listTop",{}};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
  DOCTEST_REQUIRE(r.result.as_arr().size() >= 1u);
}

DOCTEST_TEST_CASE("contract: window.listChildren") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["hwnd"] = std::string("0x1");
  CoreRequest req{"t2","window.listChildren",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: window.getInfo") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["hwnd"] = std::string("0x1");
  CoreRequest req{"t3","window.getInfo",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
  DOCTEST_REQUIRE(r.result.as_obj().at("title").as_str() == "Window A");
}

DOCTEST_TEST_CASE("contract: window.getTree") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  CoreRequest req{"t4","window.getTree",{}};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: window.pickAtPoint") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["x"] = 0.0; p["y"] = 0.0;
  CoreRequest req{"t5","window.pickAtPoint",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
  DOCTEST_REQUIRE(!r.result.as_obj().at("hwnd").as_str().empty());
}

DOCTEST_TEST_CASE("contract: window.highlight") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["hwnd"] = std::string("0x1");
  CoreRequest req{"t6","window.highlight",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: window.ensureVisible") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["hwnd"] = std::string("0x2"); p["visible"] = true;
  CoreRequest req{"t7","window.ensureVisible",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: window.ensureForeground") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["hwnd"] = std::string("0x1");
  CoreRequest req{"t8","window.ensureForeground",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: window.postMessage") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["hwnd"] = std::string("0x1"); p["msg"] = 16.0;
  CoreRequest req{"t9","window.postMessage",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: window.setProperty") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["hwnd"] = std::string("0x1"); p["name"] = std::string("topmost"); p["value"] = std::string("true");
  CoreRequest req{"t10","window.setProperty",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: window.getZOrder") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["hwnd"] = std::string("0x1");
  CoreRequest req{"t10c","window.getZOrder",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: window.move") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["hwnd"] = std::string("0x1234"); p["x"] = 100.0; p["y"] = 200.0;
  CoreRequest req{"t10a","window.move",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: window.resize") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["hwnd"] = std::string("0x1234"); p["width"] = 800.0; p["height"] = 600.0;
  CoreRequest req{"t10b","window.resize",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: window.controlClick") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["hwnd"] = std::string("0x1"); p["x"] = 10.0; p["y"] = 10.0;
  CoreRequest req{"t11","window.controlClick",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: window.controlSend") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["hwnd"] = std::string("0x1"); p["text"] = std::string("hello");
  CoreRequest req{"t12","window.controlSend",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: window.findRegex") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["title_regex"] = std::string(".*"); p["class_regex"] = std::string(".*");
  CoreRequest req{"t13","window.findRegex",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

// --- input methods ---

DOCTEST_TEST_CASE("contract: input.send") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["data_b64"] = std::string("AAAA");
  CoreRequest req{"t14","input.send",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: input.mouseClick") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["x"] = 100.0; p["y"] = 200.0;
  CoreRequest req{"t15","input.mouseClick",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: input.keyPress") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["vk"] = 65.0;
  CoreRequest req{"t16","input.keyPress",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: input.text") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["text"] = std::string("hello world");
  CoreRequest req{"t17","input.text",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: input.mouseDrag") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["start_x"] = 10.0; p["start_y"] = 10.0; p["end_x"] = 100.0; p["end_y"] = 100.0;
  CoreRequest req{"t17a","input.mouseDrag",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: input.hotkey") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["keys"] = std::string("Ctrl+C");
  CoreRequest req{"t17b","input.hotkey",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

// --- screen methods ---

DOCTEST_TEST_CASE("contract: screen.getPixel") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["x"] = 0.0; p["y"] = 0.0;
  CoreRequest req{"t18","screen.getPixel",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
  DOCTEST_REQUIRE(r.result.as_obj().at("hex").as_str() == "#FF0000");
}

DOCTEST_TEST_CASE("contract: screen.capture") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["left"] = 0.0; p["top"] = 0.0; p["right"] = 100.0; p["bottom"] = 100.0;
  CoreRequest req{"t19","screen.capture",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: screen.record") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["left"] = 0.0; p["top"] = 0.0; p["right"] = 100.0; p["bottom"] = 100.0;
  p["frames"] = 5.0; p["interval_ms"] = 10.0;
  CoreRequest req{"t19a","screen.record",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: screen.desktopInfo") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p;
  CoreRequest req{"t20a","screen.desktopInfo",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: screen.pixelSearch") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["left"] = 0.0; p["top"] = 0.0; p["right"] = 100.0; p["bottom"] = 100.0;
  p["r"] = 255.0; p["g"] = 0.0; p["b"] = 0.0;
  CoreRequest req{"t20","screen.pixelSearch",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

// --- process methods ---

DOCTEST_TEST_CASE("contract: process.list") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  CoreRequest req{"t21","process.list",{}};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
  DOCTEST_REQUIRE(r.result.as_arr().size() >= 1u);
}

DOCTEST_TEST_CASE("contract: process.kill") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["pid"] = 1234.0;
  CoreRequest req{"t22","process.kill",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: process.execute") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["command"] = std::string("cmd.exe");
  CoreRequest req{"t22a","process.execute",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

// --- file methods ---

DOCTEST_TEST_CASE("contract: file.getInfo") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["path"] = std::string("C:\\test.txt");
  CoreRequest req{"t23","file.getInfo",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: file.read") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["path"] = std::string("C:\\test.txt");
  CoreRequest req{"t24","file.read",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

// --- clipboard ---

DOCTEST_TEST_CASE("contract: clipboard.read") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  CoreRequest req{"t25","clipboard.read",{}};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: clipboard.write") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["text"] = std::string("contract test");
  CoreRequest req{"t26","clipboard.write",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

// --- services ---

DOCTEST_TEST_CASE("contract: service.list") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  CoreRequest req{"t27","service.list",{}};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: service.status") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["name"] = std::string("FakeSvc");
  CoreRequest req{"t28","service.status",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: service.control") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["name"] = std::string("FakeSvc"); p["action"] = std::string("start");
  CoreRequest req{"t29","service.control",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

// --- environment ---

DOCTEST_TEST_CASE("contract: env.get") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  CoreRequest req{"t30","env.get",{}};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: env.set") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["name"] = std::string("TEST_VAR"); p["value"] = std::string("test_value");
  CoreRequest req{"t31","env.set",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

// --- wine ---

DOCTEST_TEST_CASE("contract: wine.drives") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  CoreRequest req{"t32","wine.drives",{}};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: wine.overrides") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  CoreRequest req{"t33","wine.overrides",{}};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

// --- sync ---

DOCTEST_TEST_CASE("contract: sync.checkMutex") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["name"] = std::string("TestMutex");
  CoreRequest req{"t34","sync.checkMutex",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: sync.createMutex") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["name"] = std::string("TestMutex2"); p["own"] = true;
  CoreRequest req{"t35","sync.createMutex",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

// --- memory ---

DOCTEST_TEST_CASE("contract: mem.read") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["pid"] = 1234.0; p["address"] = 4096.0; p["size"] = 32.0;
  CoreRequest req{"t36","mem.read",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: mem.write") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["pid"] = 1234.0; p["address"] = 4096.0;
  p["data_b64"] = std::string("AAAA");
  CoreRequest req{"t37","mem.write",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

// --- image ---

DOCTEST_TEST_CASE("contract: image.match") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["left"] = 0.0; p["top"] = 0.0; p["right"] = 100.0; p["bottom"] = 100.0;
  p["sub_image_b64"] = std::string("AAAA");
  CoreRequest req{"t38","image.match",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

// --- input hook ---

DOCTEST_TEST_CASE("contract: input.hook") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["enabled"] = false;
  CoreRequest req{"t39","input.hook",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

// --- registry ---

DOCTEST_TEST_CASE("contract: reg.read") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["path"] = std::string("HKCU\\Software\\Test");
  CoreRequest req{"t40","reg.read",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: reg.write") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["path"] = std::string("HKCU\\Software\\Test");
  p["name"] = std::string("TestVal"); p["type"] = std::string("SZ"); p["data"] = std::string("hello");
  CoreRequest req{"t41","reg.write",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: reg.delete") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["path"] = std::string("HKCU\\Software\\Test");
  CoreRequest req{"t42","reg.delete",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

// --- ui automation ---

DOCTEST_TEST_CASE("contract: ui.inspect") {
  auto fb = make_fake();
  UIElementInfo el; el.automation_id = "btn"; el.name = "Button";
  fb.add_fake_ui_element(0x1, el);
  CoreEngine core(&fb);
  json::Object p; p["hwnd"] = std::string("0x1");
  CoreRequest req{"t43","ui.inspect",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: ui.invoke") {
  auto fb = make_fake();
  UIElementInfo el; el.automation_id = "btn";
  fb.add_fake_ui_element(0x1, el);
  CoreEngine core(&fb);
  json::Object p; p["hwnd"] = std::string("0x1"); p["automation_id"] = std::string("btn");
  CoreRequest req{"t44","ui.invoke",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

// --- daemon meta ---

DOCTEST_TEST_CASE("contract: daemon.health") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  CoreRequest req{"t45","daemon.health",{}};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
  DOCTEST_REQUIRE(r.result.as_obj().at("os").as_str().size() > 0);
}

DOCTEST_TEST_CASE("contract: daemon.capabilities") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  CoreRequest req{"t46","daemon.capabilities",{}};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
  DOCTEST_REQUIRE(r.result.as_obj().at("features").as_obj().at("uia").as_bool());
}

DOCTEST_TEST_CASE("contract: daemon.checkUpdate") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  CoreRequest req{"t47","daemon.checkUpdate",{}};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
}

DOCTEST_TEST_CASE("contract: daemon.downloadUpdate") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["type"] = std::string("installer");
  CoreRequest req{"t48","daemon.downloadUpdate",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(!r.ok); // fake backend returns empty path
}

DOCTEST_TEST_CASE("contract: daemon.logs") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  core.set_admin_logs_enabled(true);
  CoreRequest req{"t49","daemon.logs",{}};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
  DOCTEST_REQUIRE(r.result.is_arr());
}

DOCTEST_TEST_CASE("contract: update check returns structured data") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  core.set_admin_logs_enabled(true);
  CoreRequest req{"t50","daemon.checkUpdate",{}};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(r.ok);
  auto obj = r.result.as_obj();
  DOCTEST_REQUIRE(obj.count("update_available") > 0);
  DOCTEST_REQUIRE(obj.count("current_version") > 0);
}

DOCTEST_TEST_CASE("contract: snapshot capture + events lifecycle") {
  // Simulate what the daemon layer does: capture, subscribe, poll, unsubscribe
  // Tests the CoreEngine's response shapes for all special methods.
  auto fb = make_fake();
  CoreEngine core(&fb);

  // snapshot capture returns result (handled in daemon, here we test window.listTop as proxy)
  CoreRequest r_top{"s1","window.listTop",{}};
  auto snap = fb.capture_snapshot();
  auto resp_top = core.handle(r_top, snap);
  DOCTEST_REQUIRE(resp_top.ok);
  DOCTEST_REQUIRE(resp_top.result.as_arr().size() >= 1u);

  // events.poll with old snapshot
  auto snap2 = fb.capture_snapshot();
  json::Object pp;
  CoreRequest r_poll{"s2","events.poll",pp};
  auto resp_poll = core.handle(r_poll, snap2, &snap);
  DOCTEST_REQUIRE(resp_poll.ok);

  // session.terminate
  CoreRequest r_term{"s3","session.terminate",{}};
  auto resp_term = core.handle(r_term, snap);
  DOCTEST_REQUIRE(resp_term.ok);
}

DOCTEST_TEST_CASE("contract: bad method returns error") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  CoreRequest req{"err1","nonexistent.method",{}};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(!r.ok);
  DOCTEST_REQUIRE(r.error_code == "E_BAD_METHOD");
}

DOCTEST_TEST_CASE("contract: bad hwnd returns error") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  json::Object p; p["hwnd"] = std::string("0xDEAD");
  CoreRequest req{"err2","window.getInfo",p};
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(!r.ok);
  DOCTEST_REQUIRE(r.error_code == "E_BAD_HWND");
}

DOCTEST_TEST_CASE("contract: missing params returns error") {
  auto fb = make_fake();
  CoreEngine core(&fb);
  CoreRequest req{"err3","window.getInfo",{}};  // no hwnd
  auto r = core.handle(req, fb.capture_snapshot());
  DOCTEST_REQUIRE(!r.ok);
}
