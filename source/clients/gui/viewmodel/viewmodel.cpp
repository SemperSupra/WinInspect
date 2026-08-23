// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include "viewmodel.hpp"
#include "wininspect/tinyjson.hpp"

using wininspect::json::Array;
using wininspect::json::Object;

namespace wininspect_gui {

  static std::string dumps(const Object& o)
  {
    return wininspect::json::dumps(o);
  }

  ViewModel::ViewModel(ITransport* t) : t_(t) {}

  void ViewModel::refresh()
  {
    // Capture snapshot and list top windows
    Object cap;
    cap["id"] = "gui-1";
    cap["method"] = "snapshot.capture";
    cap["params"] = Object{};
    cap["params"].obj()["canonical"] = true;
    auto cap_raw = t_->request(dumps(cap));
    auto cap_resp = wininspect::json::parse(cap_raw).as_obj();
    if (!cap_resp.at("ok").as_bool())
      throw std::runtime_error("snapshot.capture failed: " + cap_raw);
    auto sid = cap_resp.at("result").as_obj().at("snapshot_id").as_str();

    Object req;
    req["id"] = "gui-2";
    req["method"] = "window.listTop";
    req["params"] = Object{};
    req["params"].obj()["canonical"] = true;
    req["params"].obj()["snapshot_id"] = sid;
    auto resp_raw = t_->request(dumps(req));
    auto resp = wininspect::json::parse(resp_raw).as_obj();
    if (!resp.at("ok").as_bool())
      throw std::runtime_error("window.listTop failed: " + resp_raw);

    tree_.clear();
    auto it = resp.find("result");
    if (it == resp.end() || !it->second.is_arr())
      return;
    for (const auto& e : it->second.as_arr()) {
      if (!e.is_obj())
        continue;
      Node n;
      auto it_h = e.as_obj().find("hwnd");
      n.hwnd = it_h != e.as_obj().end() && it_h->second.is_str() ? it_h->second.as_str() : "";
      n.label = n.hwnd;
      tree_.push_back(std::move(n));
    }
  }

  void ViewModel::select_hwnd(const std::string& hwnd)
  {
    // Capture snapshot and get info
    Object cap;
    cap["id"] = "gui-3";
    cap["method"] = "snapshot.capture";
    cap["params"] = Object{};
    cap["params"].obj()["canonical"] = true;
    auto cap_resp = wininspect::json::parse(t_->request(dumps(cap))).as_obj();
    auto sid = cap_resp.at("result").as_obj().at("snapshot_id").as_str();

    Object req;
    req["id"] = "gui-4";
    req["method"] = "window.getInfo";
    req["params"] = Object{};
    req["params"].obj()["canonical"] = true;
    req["params"].obj()["snapshot_id"] = sid;
    req["params"].obj()["hwnd"] = hwnd;

    auto resp = wininspect::json::parse(t_->request(dumps(req))).as_obj();
    props_.clear();
    if (resp.at("ok").as_bool()) {
      auto info = resp.at("result").as_obj();
      for (const auto& [k, v] : info) {
        Property p;
        p.key = k;
        p.value = wininspect::json::dumps(v);
        props_.push_back(std::move(p));
      }
    }
  }

} // namespace wininspect_gui
