#include "wininspect/base64.hpp"
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#include "wininspect/core.hpp"
#include <sstream>
#include <chrono>
#include <thread>
#include <iomanip>

namespace wininspect {

std::string Hwnd::to_string() const {
  std::ostringstream oss;
  oss << "0x" << std::hex << std::uppercase << val;
  return oss.str();
}

static json::Value make_error(const std::string &code, const std::string &msg) {
  json::Object e;
  e["code"] = code;
  e["message"] = msg;
  return e;
}

json::Object CoreResponse::to_json_obj(bool /*canonical*/) const {
  json::Object o;
  o["id"] = id;
  o["ok"] = ok;
  if (ok) o["result"] = result;
  else    o["error"] = make_error(error_code, error_message);
  if (!metrics.empty()) o["metrics"] = metrics;
  return o;
}

// --- parameter helpers (unchanged) ---

static std::optional<std::string> get_str(const json::Object &o, const std::string &k) {
  auto it = o.find(k);
  if (it == o.end() || !it->second.is_str()) return std::nullopt;
  return it->second.as_str();
}
static std::optional<bool> get_bool(const json::Object &o, const std::string &k) {
  auto it = o.find(k);
  if (it == o.end() || !it->second.is_bool()) return std::nullopt;
  return it->second.as_bool();
}
static std::optional<double> get_num(const json::Object &o, const std::string &k) {
  auto it = o.find(k);
  if (it == o.end() || !it->second.is_num()) return std::nullopt;
  return it->second.as_num();
}
static std::optional<hwnd_u64> parse_hwnd(const std::string &s) {
  if (s.rfind("0x", 0) != 0) return std::nullopt;
  std::uint64_t v = 0;
  std::stringstream ss;
  ss << std::hex << s.substr(2);
  ss >> v;
  if (ss.fail()) return std::nullopt;
  return (hwnd_u64)v;
}

// --- JSON converters (unchanged) ---

static json::Object event_to_json(const Event &e) {
  json::Object o;
  o["seq"] = (double)e.seq;
  o["type"] = e.type;
  o["hwnd"] = Hwnd(e.hwnd).to_string();
  if (!e.property.empty()) o["property"] = e.property;
  return o;
}

static json::Object window_node_to_json(const WindowNode &n) {
  json::Object o;
  o["hwnd"] = Hwnd(n.hwnd).to_string();
  o["title"] = n.title;
  o["class_name"] = n.class_name;
  if (!n.children.empty()) {
    json::Array arr;
    for (const auto &c : n.children) arr.push_back(window_node_to_json(c));
    o["children"] = arr;
  }
  return o;
}

static json::Object window_info_to_json(const WindowInfo &wi) {
  json::Object o;
  o["hwnd"] = Hwnd(wi.hwnd).to_string();
  o["parent"] = Hwnd(wi.parent).to_string();
  o["owner"] = Hwnd(wi.owner).to_string();
  o["class_name"] = wi.class_name;
  o["title"] = wi.title;

  auto r2j = [](const Rect &r) -> json::Object {
    json::Object o;
    o["left"] = (double)r.left;  o["top"] = (double)r.top;
    o["right"] = (double)r.right; o["bottom"] = (double)r.bottom;
    return o;
  };
  o["window_rect"] = r2j(wi.window_rect);
  o["client_rect"] = r2j(wi.client_rect);
  o["screen_rect"] = r2j(wi.screen_rect);

  o["pid"] = (double)wi.pid;
  o["tid"] = (double)wi.tid;
  o["style"] = Hwnd(wi.style).to_string();
  o["exstyle"] = Hwnd(wi.exstyle).to_string();

  json::Array sf, esf;
  for (const auto &s : wi.style_flags) sf.push_back(s);
  for (const auto &s : wi.ex_style_flags) esf.push_back(s);
  o["style_flags"] = sf;
  o["ex_style_flags"] = esf;

  o["visible"] = wi.visible;
  o["enabled"] = wi.enabled;
  o["iconic"] = wi.iconic;
  o["zoomed"] = wi.zoomed;
  o["process_image"] = wi.process_image;
  return o;
}

static json::Object ui_element_to_json(const UIElementInfo &el) {
  json::Object o;
  o["automation_id"] = el.automation_id;
  o["name"] = el.name;
  o["class_name"] = el.class_name;
  o["control_type"] = el.control_type;

  json::Object r;
  r["left"] = (double)el.bounding_rect.left;
  r["top"] = (double)el.bounding_rect.top;
  r["right"] = (double)el.bounding_rect.right;
  r["bottom"] = (double)el.bounding_rect.bottom;
  o["bounding_rect"] = r;
  o["enabled"] = el.enabled;
  o["visible"] = el.visible;

  if (!el.children.empty()) {
    json::Array arr;
    for (const auto &c : el.children) arr.push_back(ui_element_to_json(c));
    o["children"] = arr;
  }
  return o;
}

// --- dispatch table builder ---

void CoreEngine::build_dispatch_table() {
  // Bind IBackend* via capture; daemon ensures the backend outlives CoreEngine.
  IBackend *b = backend_;  // stored in the class, but we capture by member ptr via this

  auto ok_json = [](bool v) -> json::Value {
    json::Object o; o["ok"] = v; return o;
  };
  auto sent_json = [](bool v) -> json::Value {
    json::Object o; o["sent"] = v; return o;
  };
  auto changed_json = [](bool v) -> json::Value {
    json::Object o; o["changed"] = v; return o;
  };

  // --- snapshot + events (handled in daemon layer) ---
  dispatch_["events.poll"] = [&](  const CoreRequest &req,
                                  const Snapshot &snap, const Snapshot *old) -> CoreResponse {
    CoreResponse resp;
    resp.id = req.id;
    if (!old) throw std::runtime_error("events.poll requires two snapshots");
    auto wait_ms = get_num(req.params, "wait_ms").value_or(0);
    auto interval_ms = get_num(req.params, "interval_ms").value_or(100);
    auto start = std::chrono::steady_clock::now();
    while (true) {
      auto events = b->poll_events(*old, snap);
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start).count();
      if (!events.empty() || wait_ms == 0 || elapsed >= (long long)wait_ms) {
        json::Array arr;
        for (size_t i = 0; i < events.size(); ++i) {
          events[i].seq = i + 1;
          arr.push_back(event_to_json(events[i]));
        }
        resp.ok = true; resp.result = arr; return resp;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds((int)interval_ms));
      break;
    }
    resp.ok = true; resp.result = json::Array{}; return resp;
  };

  dispatch_["session.terminate"] = []( const CoreRequest &,
                                        const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    json::Object o; o["terminated"] = true;
    resp.ok = true; resp.result = o; return resp;
  };

  // --- window methods ---
  dispatch_["window.listTop"] = [&](  const CoreRequest &,
                                      const Snapshot &snap, const Snapshot *) {
    CoreResponse resp;
    auto top = b->list_top(snap);
    json::Array arr;
    for (auto h : top) { json::Object e; e["hwnd"] = Hwnd(h).to_string(); arr.emplace_back(e); }
    resp.ok = true; resp.result = arr; return resp;
  };

  dispatch_["window.listChildren"] = [&](  const CoreRequest &req,
                                          const Snapshot &snap, const Snapshot *) {
    CoreResponse resp;
    auto hwnd_s = get_str(req.params, "hwnd");
    if (!hwnd_s) throw std::runtime_error("missing hwnd");
    auto hwnd = parse_hwnd(*hwnd_s);
    if (!hwnd) throw std::runtime_error("bad hwnd");
    auto ch = b->list_children(snap, *hwnd);
    json::Array arr;
    for (auto h : ch) { json::Object e; e["hwnd"] = Hwnd(h).to_string(); arr.emplace_back(e); }
    resp.ok = true; resp.result = arr; return resp;
  };

  dispatch_["window.getTree"] = [&](  const CoreRequest &req,
                                     const Snapshot &snap, const Snapshot *) {
    CoreResponse resp;
    auto hwnd_s = get_str(req.params, "hwnd");
    hwnd_u64 root = 0;
    if (hwnd_s) { auto h = parse_hwnd(*hwnd_s); if (!h) throw std::runtime_error("bad hwnd"); root = *h; }
    auto nodes = b->get_window_tree(snap, root);
    json::Array arr;
    for (const auto &n : nodes) arr.push_back(window_node_to_json(n));
    resp.ok = true; resp.result = arr; return resp;
  };

  dispatch_["window.highlight"] = [&](  const CoreRequest &req,
                                       const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto hwnd_s = get_str(req.params, "hwnd");
    if (!hwnd_s) throw std::runtime_error("missing hwnd");
    auto hwnd = parse_hwnd(*hwnd_s);
    if (!hwnd) throw std::runtime_error("bad hwnd");
    json::Object o; o["highlighted"] = b->highlight_window(*hwnd);
    resp.ok = true; resp.result = o; return resp;
  };

  dispatch_["window.getInfo"] = [&](  const CoreRequest &req,
                                     const Snapshot &snap, const Snapshot *) {
    CoreResponse resp;
    auto hwnd_s = get_str(req.params, "hwnd");
    if (!hwnd_s) throw std::runtime_error("missing hwnd");
    auto hwnd = parse_hwnd(*hwnd_s);
    if (!hwnd) throw std::runtime_error("bad hwnd");
    auto info = b->get_info(snap, *hwnd);
    if (!info) { resp.ok = false; resp.error_code = "E_BAD_HWND"; resp.error_message = "not a valid window handle"; return resp; }
    resp.ok = true; resp.result = window_info_to_json(*info); return resp;
  };

  dispatch_["window.pickAtPoint"] = [&](  const CoreRequest &req,
                                         const Snapshot &snap, const Snapshot *) {
    CoreResponse resp;
    auto x = get_num(req.params, "x"), y = get_num(req.params, "y");
    if (!x || !y) throw std::runtime_error("missing x/y");
    PickFlags flags;
    if (auto bv = get_bool(req.params, "prefer_child")) flags.prefer_child = *bv;
    if (auto bv = get_bool(req.params, "ignore_transparent")) flags.ignore_transparent = *bv;
    auto h = b->pick_at_point(snap, (int)*x, (int)*y, flags);
    if (!h) { resp.ok = false; resp.error_code = "E_NOT_FOUND"; resp.error_message = "no window at point"; return resp; }
    json::Object o; o["hwnd"] = Hwnd(*h).to_string();
    resp.ok = true; resp.result = o; return resp;
  };

  // --- desired-state actions (idempotent) ---
  dispatch_["window.ensureVisible"] = [&](  const CoreRequest &req,
                                           const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto hwnd_s = get_str(req.params, "hwnd"); auto vis = get_bool(req.params, "visible");
    if (!hwnd_s || !vis) throw std::runtime_error("missing hwnd/visible");
    auto hwnd = parse_hwnd(*hwnd_s);
    if (!hwnd) throw std::runtime_error("bad hwnd");
    resp.ok = true; resp.result = changed_json(b->ensure_visible(*hwnd, *vis).changed);
    return resp;
  };

  dispatch_["window.ensureForeground"] = [&](  const CoreRequest &req,
                                              const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto hwnd_s = get_str(req.params, "hwnd");
    if (!hwnd_s) throw std::runtime_error("missing hwnd");
    auto hwnd = parse_hwnd(*hwnd_s);
    if (!hwnd) throw std::runtime_error("bad hwnd");
    resp.ok = true; resp.result = changed_json(b->ensure_foreground(*hwnd).changed);
    return resp;
  };

  dispatch_["window.setProperty"] = [&](  const CoreRequest &req,
                                         const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto hwnd_s = get_str(req.params, "hwnd"); auto name = get_str(req.params, "name"); auto val = get_str(req.params, "value");
    if (!hwnd_s || !name || !val) throw std::runtime_error("missing hwnd/name/value");
    auto hwnd = parse_hwnd(*hwnd_s);
    if (!hwnd) throw std::runtime_error("bad hwnd");
    resp.ok = true; resp.result = ok_json(b->set_property(*hwnd, *name, *val));
    return resp;
  };

  dispatch_["window.postMessage"] = [&](  const CoreRequest &req,
                                         const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto hwnd_s = get_str(req.params, "hwnd"); auto msg = get_num(req.params, "msg");
    if (!hwnd_s || !msg) throw std::runtime_error("missing hwnd/msg");
    auto hwnd = parse_hwnd(*hwnd_s);
    if (!hwnd) throw std::runtime_error("bad hwnd");
    auto wparam = get_num(req.params, "wparam"), lparam = get_num(req.params, "lparam");
    resp.ok = true; resp.result = sent_json(
        b->post_message(*hwnd, (uint32_t)*msg,
                        (uint64_t)(wparam.value_or(0)), (uint64_t)(lparam.value_or(0))));
    return resp;
  };

  // --- input methods ---
  dispatch_["input.send"] = [&](  const CoreRequest &req,
                                 const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto data_b64 = get_str(req.params, "data_b64");
    if (!data_b64) throw std::runtime_error("missing data_b64");
    resp.ok = true; resp.result = sent_json(b->send_input(base64::decode(*data_b64)));
    return resp;
  };

  dispatch_["input.mouseClick"] = [&](  const CoreRequest &req,
                                       const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto x = get_num(req.params, "x"), y = get_num(req.params, "y");
    if (!x || !y) throw std::runtime_error("missing x/y");
    int btn = (int)get_num(req.params, "button").value_or(0);
    resp.ok = true; resp.result = sent_json(b->send_mouse_click((int)*x, (int)*y, btn));
    return resp;
  };

  dispatch_["input.keyPress"] = [&](  const CoreRequest &req,
                                     const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto vk = get_num(req.params, "vk");
    if (!vk) throw std::runtime_error("missing vk");
    resp.ok = true; resp.result = sent_json(b->send_key_press((int)*vk));
    return resp;
  };

  dispatch_["input.text"] = [&](  const CoreRequest &req,
                                 const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto text = get_str(req.params, "text");
    if (!text) throw std::runtime_error("missing text");
    resp.ok = true; resp.result = sent_json(b->send_text(*text));
    return resp;
  };

  // --- stealth input ---
  dispatch_["window.controlClick"] = [&](  const CoreRequest &req,
                                          const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto hwnd_s = get_str(req.params, "hwnd"); auto x = get_num(req.params, "x"); auto y = get_num(req.params, "y");
    if (!hwnd_s || !x || !y) throw std::runtime_error("missing hwnd/x/y");
    auto hwnd = parse_hwnd(*hwnd_s);
    if (!hwnd) throw std::runtime_error("bad hwnd");
    int btn = (int)get_num(req.params, "button").value_or(0);
    resp.ok = true; resp.result = sent_json(b->control_click(*hwnd, (int)*x, (int)*y, btn));
    return resp;
  };

  dispatch_["window.controlSend"] = [&](  const CoreRequest &req,
                                         const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto hwnd_s = get_str(req.params, "hwnd"); auto text = get_str(req.params, "text");
    if (!hwnd_s || !text) throw std::runtime_error("missing hwnd/text");
    auto hwnd = parse_hwnd(*hwnd_s);
    if (!hwnd) throw std::runtime_error("bad hwnd");
    resp.ok = true; resp.result = sent_json(b->control_send(*hwnd, *text));
    return resp;
  };

  // --- screen methods ---
  dispatch_["screen.getPixel"] = [&](  const CoreRequest &req,
                                      const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto x = get_num(req.params, "x"), y = get_num(req.params, "y");
    if (!x || !y) throw std::runtime_error("missing x/y");
    auto c = b->get_pixel((int)*x, (int)*y);
    if (!c) throw std::runtime_error("failed to get pixel");
    json::Object o;
    o["hex"] = c->to_hex(); o["r"] = (double)c->r; o["g"] = (double)c->g; o["b"] = (double)c->b;
    resp.ok = true; resp.result = o; return resp;
  };

  dispatch_["screen.capture"] = [&](  const CoreRequest &req,
                                     const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto l = get_num(req.params, "left"), t = get_num(req.params, "top"),
         r = get_num(req.params, "right"), bm = get_num(req.params, "bottom");
    if (!l || !t || !r || !bm) throw std::runtime_error("missing region");
    Rect rect{(long)*l, (long)*t, (long)*r, (long)*bm};
    auto sc = b->capture_screen(rect);
    if (!sc) throw std::runtime_error("capture failed");
    json::Object o;
    o["width"] = (double)sc->width; o["height"] = (double)sc->height; o["data_b64"] = sc->data_b64;
    resp.ok = true; resp.result = o; return resp;
  };

  dispatch_["screen.pixelSearch"] = [&](  const CoreRequest &req,
                                         const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto l = get_num(req.params, "left"), t = get_num(req.params, "top"),
         r = get_num(req.params, "right"), bm = get_num(req.params, "bottom"),
         rv = get_num(req.params, "r"), gv = get_num(req.params, "g"), bv = get_num(req.params, "b");
    if (!l || !t || !r || !bm || !rv || !gv || !bv) throw std::runtime_error("missing parameters");
    Rect rect{(long)*l, (long)*t, (long)*r, (long)*bm};
    Color target{(uint8_t)*rv, (uint8_t)*gv, (uint8_t)*bv};
    int var = (int)get_num(req.params, "variation").value_or(0);
    auto res = b->pixel_search(rect, target, var);
    if (res) { json::Object o; o["x"] = (double)res->first; o["y"] = (double)res->second; resp.ok = true; resp.result = o; }
    else { resp.ok = false; resp.error_code = "E_NOT_FOUND"; resp.error_message = "color not found in region"; }
    return resp;
  };

  // --- process ---
  dispatch_["process.list"] = [&](  const CoreRequest &,
                                   const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto procs = b->list_processes();
    json::Array arr;
    for (const auto &p : procs) {
      json::Object o; o["pid"] = (double)p.pid; o["name"] = p.name; o["path"] = p.path; arr.push_back(o);
    }
    resp.ok = true; resp.result = arr; return resp;
  };

  dispatch_["process.kill"] = [&](  const CoreRequest &req,
                                   const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto pid = get_num(req.params, "pid");
    if (!pid) throw std::runtime_error("missing pid");
    resp.ok = true; resp.result = ok_json(b->kill_process((uint32_t)*pid));
    return resp;
  };

  // --- file ---
  dispatch_["file.getInfo"] = [&](  const CoreRequest &req,
                                   const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto path = get_str(req.params, "path");
    if (!path) throw std::runtime_error("missing path");
    auto fi = b->get_file_info(*path);
    if (!fi) { resp.ok = false; resp.error_code = "E_NOT_FOUND"; return resp; }
    json::Object o; o["path"] = fi->path; o["size"] = (double)fi->size; o["is_directory"] = fi->is_directory;
    resp.ok = true; resp.result = o; return resp;
  };

  dispatch_["file.read"] = [&](  const CoreRequest &req,
                                const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto path = get_str(req.params, "path");
    if (!path) throw std::runtime_error("missing path");
    auto content = b->read_file_content(*path);
    if (!content) { resp.ok = false; resp.error_code = "E_READ_FAILED"; return resp; }
    json::Object o; o["content_b64"] = base64::encode(std::vector<uint8_t>(content->begin(), content->end()));
    resp.ok = true; resp.result = o; return resp;
  };

  // --- clipboard ---
  dispatch_["clipboard.read"] = [&](  const CoreRequest &,
                                     const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto text = b->clipboard_read();
    json::Object o; if (text) o["text"] = *text;
    resp.ok = true; resp.result = o; return resp;
  };

  dispatch_["clipboard.write"] = [&](  const CoreRequest &req,
                                      const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto text = get_str(req.params, "text");
    if (!text) throw std::runtime_error("missing text");
    resp.ok = true; resp.result = ok_json(b->clipboard_write(*text));
    return resp;
  };

  // --- services ---
  dispatch_["service.list"] = [&](  const CoreRequest &,
                                   const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto svcs = b->service_list();
    json::Array arr;
    for (const auto &s : svcs) {
      json::Object o; o["name"] = s.name; o["display_name"] = s.display_name; o["state"] = s.state; arr.push_back(o);
    }
    resp.ok = true; resp.result = arr; return resp;
  };

  dispatch_["service.status"] = [&](  const CoreRequest &req,
                                     const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto name = get_str(req.params, "name");
    if (!name) throw std::runtime_error("missing name");
    json::Object o; o["status"] = b->service_status(*name);
    resp.ok = true; resp.result = o; return resp;
  };

  dispatch_["service.control"] = [&](  const CoreRequest &req,
                                      const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto name = get_str(req.params, "name"), action = get_str(req.params, "action");
    if (!name || !action) throw std::runtime_error("missing name/action");
    resp.ok = true; resp.result = ok_json(b->service_control(*name, *action));
    return resp;
  };

  // --- env ---
  dispatch_["env.get"] = [&](  const CoreRequest &,
                              const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto vars = b->env_get_all();
    json::Object o;
    for (const auto &v : vars) o[v.name] = v.value;
    resp.ok = true; resp.result = o; return resp;
  };

  dispatch_["env.set"] = [&](  const CoreRequest &req,
                              const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto name = get_str(req.params, "name"), val = get_str(req.params, "value");
    if (!name || !val) throw std::runtime_error("missing name/value");
    resp.ok = true; resp.result = ok_json(b->env_set(*name, *val));
    return resp;
  };

  // --- wine ---
  dispatch_["wine.drives"] = [&](  const CoreRequest &,
                                  const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto drives = b->wine_get_drives();
    json::Array arr;
    for (const auto &d : drives) {
      json::Object o; o["letter"] = d.letter; o["mapping"] = d.mapping; o["type"] = d.type; arr.push_back(o);
    }
    resp.ok = true; resp.result = arr; return resp;
  };

  dispatch_["wine.overrides"] = [&](  const CoreRequest &,
                                     const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto ovr = b->wine_get_overrides();
    json::Array arr; for (const auto &s : ovr) arr.push_back(s);
    resp.ok = true; resp.result = arr; return resp;
  };

  // --- sync ---
  dispatch_["sync.checkMutex"] = [&](  const CoreRequest &req,
                                      const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto name = get_str(req.params, "name");
    if (!name) throw std::runtime_error("missing name");
    json::Object o; o["exists"] = b->sync_check_mutex(*name);
    resp.ok = true; resp.result = o; return resp;
  };

  dispatch_["sync.createMutex"] = [&](  const CoreRequest &req,
                                       const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto name = get_str(req.params, "name");
    if (!name) throw std::runtime_error("missing name");
    bool own = get_bool(req.params, "own").value_or(true);
    json::Object o; o["created"] = b->sync_create_mutex(*name, own);
    resp.ok = true; resp.result = o; return resp;
  };

  // --- memory ---
  dispatch_["mem.read"] = [&](  const CoreRequest &req,
                               const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto pid = get_num(req.params, "pid"), addr = get_num(req.params, "address"), sz = get_num(req.params, "size");
    if (!pid || !addr || !sz) throw std::runtime_error("missing parameters");
    auto res = b->mem_read((uint32_t)*pid, (uint64_t)*addr, (size_t)*sz);
    if (!res) { resp.ok = false; return resp; }
    json::Object o; o["address"] = (double)res->address; o["data_b64"] = res->data_b64;
    resp.ok = true; resp.result = o; return resp;
  };

  dispatch_["mem.write"] = [&](  const CoreRequest &req,
                                const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto pid = get_num(req.params, "pid"); auto addr = get_num(req.params, "address"); auto data_b64 = get_str(req.params, "data_b64");
    if (!pid || !addr || !data_b64) throw std::runtime_error("missing parameters");
    auto data = base64::decode(*data_b64);
    resp.ok = true; resp.result = ok_json(b->mem_write((uint32_t)*pid, (uint64_t)*addr, data));
    return resp;
  };

  // --- image ---
  dispatch_["image.match"] = [&](  const CoreRequest &req,
                                  const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto l = get_num(req.params, "left"), t = get_num(req.params, "top"),
         r = get_num(req.params, "right"), bm = get_num(req.params, "bottom");
    auto sub_b64 = get_str(req.params, "sub_image_b64");
    if (!l || !t || !r || !bm || !sub_b64) throw std::runtime_error("missing parameters");
    Rect rect{(long)*l, (long)*t, (long)*r, (long)*bm};
    auto sub = base64::decode(*sub_b64);
    auto res = b->image_match(rect, sub);
    if (!res) { resp.ok = false; return resp; }
    json::Object o; o["x"] = (double)res->x; o["y"] = (double)res->y; o["confidence"] = res->confidence;
    resp.ok = true; resp.result = o; return resp;
  };

  // --- input hook ---
  dispatch_["input.hook"] = [&](  const CoreRequest &req,
                                 const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto enabled = get_bool(req.params, "enabled");
    if (!enabled) throw std::runtime_error("missing enabled");
    resp.ok = true; resp.result = ok_json(b->input_hook_enable(*enabled));
    return resp;
  };

  // --- regex ---
  dispatch_["window.findRegex"] = [&](  const CoreRequest &req,
                                       const Snapshot &snap, const Snapshot *) {
    CoreResponse resp;
    auto t_re = get_str(req.params, "title_regex").value_or(".*");
    auto c_re = get_str(req.params, "class_regex").value_or(".*");
    auto hwnds = b->find_windows_regex(t_re, c_re);
    json::Array arr;
    for (auto h : hwnds) { json::Object e; e["hwnd"] = Hwnd(h).to_string(); arr.push_back(e); }
    resp.ok = true; resp.result = arr; return resp;
  };

  // --- registry ---
  dispatch_["reg.read"] = [&](  const CoreRequest &req,
                               const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto path = get_str(req.params, "path");
    if (!path) throw std::runtime_error("missing path");
    auto res = b->reg_read(*path);
    if (!res) { resp.ok = false; resp.error_code = "E_NOT_FOUND"; return resp; }
    json::Object o; o["path"] = res->path;
    json::Array sk; for (const auto &s : res->subkeys) sk.push_back(s);
    o["subkeys"] = sk;
    json::Array vals;
    for (const auto &v : res->values) {
      json::Object vo; vo["name"] = v.name; vo["type"] = v.type; vo["data"] = v.data; vals.push_back(vo);
    }
    o["values"] = vals;
    resp.ok = true; resp.result = o; return resp;
  };

  dispatch_["reg.write"] = [&](  const CoreRequest &req,
                                const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto path = get_str(req.params, "path"), name = get_str(req.params, "name"),
         type = get_str(req.params, "type"), data = get_str(req.params, "data");
    if (!path || !name || !type || !data) throw std::runtime_error("missing parameters");
    RegistryValue rv; rv.name = *name; rv.type = *type; rv.data = *data;
    resp.ok = true; resp.result = ok_json(b->reg_write(*path, rv));
    return resp;
  };

  dispatch_["reg.delete"] = [&](  const CoreRequest &req,
                                 const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto path = get_str(req.params, "path");
    if (!path) throw std::runtime_error("missing path");
    auto name = get_str(req.params, "name").value_or("");
    resp.ok = true; resp.result = ok_json(b->reg_delete(*path, name));
    return resp;
  };

  // --- ui automation ---
  dispatch_["ui.inspect"] = [&](  const CoreRequest &req,
                                 const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto hwnd_s = get_str(req.params, "hwnd");
    if (!hwnd_s) throw std::runtime_error("missing hwnd");
    auto hwnd = parse_hwnd(*hwnd_s);
    if (!hwnd) throw std::runtime_error("bad hwnd");
    auto elements = b->inspect_ui_elements(*hwnd);
    json::Array arr;
    for (const auto &el : elements) arr.push_back(ui_element_to_json(el));
    resp.ok = true; resp.result = arr; return resp;
  };

  dispatch_["ui.invoke"] = [&](  const CoreRequest &req,
                                const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto hwnd_s = get_str(req.params, "hwnd"); auto aid = get_str(req.params, "automation_id");
    if (!hwnd_s || !aid) throw std::runtime_error("missing hwnd/automation_id");
    auto hwnd = parse_hwnd(*hwnd_s);
    if (!hwnd) throw std::runtime_error("bad hwnd");
    json::Object o; o["invoked"] = b->invoke_ui_element(*hwnd, *aid);
    resp.ok = true; resp.result = o; return resp;
  };

  // --- daemon meta ---
  dispatch_["daemon.health"] = [&](  const CoreRequest &,
                                    const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    resp.ok = true; resp.result = b->get_env_metadata(); return resp;
  };

  dispatch_["daemon.capabilities"] = [&](  const CoreRequest &,
                                          const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto caps = b->get_capabilities();
    json::Object o;
    o["os"] = caps.os; o["is_wine"] = caps.is_wine; o["arch"] = caps.arch;
    o["win_major"] = (double)caps.win_major; o["win_minor"] = (double)caps.win_minor; o["win_build"] = (double)caps.win_build;
    if (!caps.wine_version.empty()) o["wine_version"] = caps.wine_version;
    json::Object features;
    features["uia"] = caps.uia_available; features["clipboard"] = caps.clipboard_available;
    features["registry_write"] = caps.registry_writable; features["service_manager"] = caps.service_manager;
    features["process_memory"] = caps.process_memory; features["input_injection"] = caps.input_injection;
    features["window_highlight"] = caps.window_highlight;
    o["features"] = features;
    resp.ok = true; resp.result = o; return resp;
  };

  dispatch_["daemon.checkUpdate"] = [&](  const CoreRequest &,
                                         const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto info = b->check_for_update();
    json::Object o;
    o["update_available"] = info.update_available; o["current_version"] = info.current_version;
    o["latest_version"] = info.latest_version; o["installer_url"] = info.installer_url;
    o["portable_zip_url"] = info.portable_zip_url; o["release_notes"] = info.release_notes;
    if (!info.error.empty()) o["error"] = info.error;
    resp.ok = true; resp.result = o; return resp;
  };

  dispatch_["daemon.downloadUpdate"] = [&](  const CoreRequest &req,
                                            const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto url = get_str(req.params, "url").value_or("");
    auto type_hint = get_str(req.params, "type").value_or("installer");
    auto path = b->download_update(url, type_hint);
    json::Object o;
    if (path.empty()) { resp.ok = false; o["ok"] = false; o["error"] = "download failed"; }
    else { o["ok"] = true; o["path"] = path; }
    resp.result = o; return resp;
  };

  dispatch_["daemon.logs"] = []( const CoreRequest &,
                                 const Snapshot &, const Snapshot *) {
    CoreResponse resp;
    auto logs = Logger::get().get_recent_logs();
    json::Array arr;
    for (const auto &l : logs) {
      json::Object lo;
      lo["timestamp"] = l.timestamp; lo["level"] = (double)static_cast<int>(l.level); lo["message"] = l.message;
      arr.push_back(lo);
    }
    resp.ok = true; resp.result = arr; return resp;
  };
}

// --- public interface ---

CoreEngine::CoreEngine(IBackend *backend) : backend_(backend) {
  build_dispatch_table();
}

CoreResponse CoreEngine::handle(const CoreRequest &req,
                                const Snapshot &snapshot,
                                const Snapshot *old_snapshot) {
  auto start_time = std::chrono::steady_clock::now();
  LOG_DEBUG("Handling request: " + req.method + " (id=" + req.id + ")");

  CoreResponse resp;
  resp.id = req.id;
  resp.ok = true;
  resp.result = json::Null{};

  try {
    auto it = dispatch_.find(req.method);
    if (it != dispatch_.end()) {
      resp = it->second(req, snapshot, old_snapshot);
    } else {
      resp.ok = false;
      resp.error_code = "E_BAD_METHOD";
      resp.error_message = "method not implemented in core";
      LOG_WARN("Method not implemented: " + req.method);
    }
  } catch (const std::exception &e) {
    resp.ok = false;
    resp.error_code = "E_BAD_REQUEST";
    resp.error_message = e.what();
    LOG_ERROR("Request failed: " + std::string(e.what()));
  }

  auto end_time = std::chrono::steady_clock::now();
  auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
  resp.metrics["duration_ms"] = (double)duration_ms;

  return resp;
}

CoreRequest parse_request_json(std::string_view json_utf8) {
  auto v = json::parse(json_utf8);
  if (!v.is_obj()) throw std::runtime_error("request must be object");
  const auto &o = v.as_obj();

  auto it_id = o.find("id"), it_m = o.find("method"), it_p = o.find("params");
  if (it_id == o.end() || it_m == o.end() || it_p == o.end())
    throw std::runtime_error("missing fields");
  if (!it_id->second.is_str() || !it_m->second.is_str() || !it_p->second.is_obj())
    throw std::runtime_error("bad field types");

  CoreRequest r;
  r.id = it_id->second.as_str();
  r.method = it_m->second.as_str();
  r.params = it_p->second.as_obj();
  return r;
}

std::string serialize_response_json(const CoreResponse &resp, bool canonical) {
  (void)canonical;
  return json::dumps(resp.to_json_obj(canonical));
}

} // namespace wininspect
