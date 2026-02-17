#include "../viewmodel/viewmodel.hpp"
#include "doctest/doctest.h"
#include "wininspect/tinyjson.hpp"
#include <map>

using namespace wininspect_gui;

struct FakeTransport : ITransport {
  // Very small fake: returns snapshot_id "s-1", listTop two windows, getInfo
  // fixed.
  int snap = 0;
  std::string request(const std::string &json) override {
    auto req = wininspect::json::parse(json).as_obj();
    auto method = req.at("method").as_str();
    if (method == "snapshot.capture") {
      snap++;
      wininspect::json::Object resp;
      resp["id"] = req.at("id").as_str();
      resp["ok"] = true;
      wininspect::json::Object r;
      r["snapshot_id"] = std::string("s-") + std::to_string(snap);
      resp["result"] = r;
      return wininspect::json::dumps(resp);
    }
    if (method == "window.listTop") {
      wininspect::json::Object resp;
      resp["id"] = req.at("id").as_str();
      resp["ok"] = true;
      wininspect::json::Array arr;
      wininspect::json::Object a;
      a["hwnd"] = "0x1";
      wininspect::json::Object b;
      b["hwnd"] = "0x2";
      arr.push_back(a);
      arr.push_back(b);
      resp["result"] = arr;
      return wininspect::json::dumps(resp);
    }
    if (method == "window.getInfo") {
      wininspect::json::Object resp;
      resp["id"] = req.at("id").as_str();
      resp["ok"] = true;
      wininspect::json::Object info;
      info["hwnd"] = req.at("params").as_obj().at("hwnd").as_str();
      info["class_name"] = "C1";
      info["title"] = "T";
      info["parent"] = "0x0";
      info["owner"] = "0x0";
      info["window_rect"] = wininspect::json::Object{};
      info["client_rect"] = wininspect::json::Object{};
      info["pid"] = 123.0;
      info["tid"] = 456.0;
      info["style"] = "0x0";
      info["exstyle"] = "0x0";
      info["visible"] = true;
      info["enabled"] = true;
      info["iconic"] = false;
      info["zoomed"] = false;
      info["process_image"] = "fake.exe";
      resp["result"] = info;
      return wininspect::json::dumps(resp);
    }
    wininspect::json::Object err;
    err["id"] = req.at("id").as_str();
    err["ok"] = false;
    wininspect::json::Object e;
    e["code"] = "E_BAD_METHOD";
    e["message"] = "bad";
    err["error"] = e;
    return wininspect::json::dumps(err);
  }
};

DOCTEST_TEST_CASE("ViewModel refresh populates tree") {
  FakeTransport t;
  ViewModel vm(&t);
  vm.refresh();
  DOCTEST_REQUIRE_EQ(vm.tree().size(), 2u);
  DOCTEST_REQUIRE_EQ(vm.tree()[0].hwnd, "0x1");
}

DOCTEST_TEST_CASE("ViewModel select populates props") {
  FakeTransport t;
  ViewModel vm(&t);
  vm.select_hwnd("0x2");
  DOCTEST_REQUIRE(vm.props().size() > 0);
}
