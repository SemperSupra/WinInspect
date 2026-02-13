#include "wininspect/core.hpp"
#include <sstream>

namespace wininspect {

static json::Value make_error(const std::string& code, const std::string& msg) {
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
  else o["error"] = make_error(error_code, error_message);
  return o;
}

CoreEngine::CoreEngine(IBackend* backend) : backend_(backend) {}

static std::optional<std::string> get_str(const json::Object& o, const std::string& k) {
  auto it = o.find(k);
  if (it == o.end()) return std::nullopt;
  if (!it->second.is_str()) return std::nullopt;
  return it->second.as_str();
}
static std::optional<bool> get_bool(const json::Object& o, const std::string& k) {
  auto it = o.find(k);
  if (it == o.end()) return std::nullopt;
  if (!it->second.is_bool()) return std::nullopt;
  return it->second.as_bool();
}
static std::optional<double> get_num(const json::Object& o, const std::string& k) {
  auto it = o.find(k);
  if (it == o.end()) return std::nullopt;
  if (!it->second.is_num()) return std::nullopt;
  return it->second.as_num();
}

static std::optional<hwnd_u64> parse_hwnd(const std::string& s) {
  if (s.rfind("0x", 0) != 0) return std::nullopt;
  std::uint64_t v = 0;
  std::stringstream ss;
  ss << std::hex << s.substr(2);
  ss >> v;
  if (ss.fail()) return std::nullopt;
  return (hwnd_u64)v;
}

static std::string fmt_hwnd(hwnd_u64 h) {
  std::ostringstream oss;
  oss << "0x" << std::hex << std::uppercase << (std::uint64_t)h;
  return oss.str();
}

static json::Object event_to_json(const Event& e) {
  json::Object o;
  o["type"] = e.type;
  o["hwnd"] = fmt_hwnd(e.hwnd);
  if (!e.property.empty()) o["property"] = e.property;
  return o;
}

static json::Object window_info_to_json(const WindowInfo& wi) {
  json::Object o;
  o["hwnd"] = fmt_hwnd(wi.hwnd);
  o["parent"] = fmt_hwnd(wi.parent);
  o["owner"] = fmt_hwnd(wi.owner);
  o["class_name"] = wi.class_name;
  o["title"] = wi.title;

  json::Object wr; wr["left"]= (double)wi.window_rect.left; wr["top"]=(double)wi.window_rect.top;
  wr["right"]=(double)wi.window_rect.right; wr["bottom"]=(double)wi.window_rect.bottom;
  o["window_rect"] = wr;

  json::Object cr; cr["left"]= (double)wi.client_rect.left; cr["top"]=(double)wi.client_rect.top;
  cr["right"]=(double)wi.client_rect.right; cr["bottom"]=(double)wi.client_rect.bottom;
  o["client_rect"] = cr;

  o["pid"] = (double)wi.pid;
  o["tid"] = (double)wi.tid;

  o["style"] = fmt_hwnd((hwnd_u64)wi.style);
  o["exstyle"] = fmt_hwnd((hwnd_u64)wi.exstyle);

  o["visible"] = wi.visible;
  o["enabled"] = wi.enabled;
  o["iconic"] = wi.iconic;
  o["zoomed"] = wi.zoomed;

  o["process_image"] = wi.process_image;
  return o;
}

static std::vector<uint8_t> base64_decode(std::string_view in) {
  static const std::string_view b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::vector<uint8_t> out;
  std::vector<int> T(256, -1);
  for (int i = 0; i < 64; i++) T[b64[i]] = i;

  int val = 0, valb = -8;
  for (char c : in) {
    if (T[c] == -1) break;
    val = (val << 6) + T[c];
    valb += 6;
    if (valb >= 0) {
      out.push_back(uint8_t((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return out;
}

CoreResponse CoreEngine::handle(const CoreRequest& req, const Snapshot& snapshot, const Snapshot* old_snapshot) {
  CoreResponse resp;
  resp.id = req.id;
  resp.ok = true;
  resp.result = json::Null{};

  try {
    if (req.method == "events.poll") {
      if (!old_snapshot) throw std::runtime_error("events.poll requires two snapshots");
      auto events = backend_->poll_events(*old_snapshot, snapshot);
      json::Array arr;
      for (const auto& e : events) arr.push_back(event_to_json(e));
      resp.result = arr;
      return resp;
    }

    if (req.method == "window.listTop") {
      auto top = backend_->list_top(snapshot);
      json::Array arr;
      for (auto h : top) {
        json::Object e;
        e["hwnd"] = fmt_hwnd(h);
        arr.emplace_back(e);
      }
      resp.result = arr;
      return resp;
    }

    if (req.method == "window.listChildren") {
      auto hwnd_s = get_str(req.params, "hwnd");
      if (!hwnd_s) throw std::runtime_error("missing hwnd");
      auto hwnd = parse_hwnd(*hwnd_s);
      if (!hwnd) throw std::runtime_error("bad hwnd");
      auto ch = backend_->list_children(snapshot, *hwnd);
      json::Array arr;
      for (auto h : ch) {
        json::Object e;
        e["hwnd"] = fmt_hwnd(h);
        arr.emplace_back(e);
      }
      resp.result = arr;
      return resp;
    }

    if (req.method == "window.getInfo") {
      auto hwnd_s = get_str(req.params, "hwnd");
      if (!hwnd_s) throw std::runtime_error("missing hwnd");
      auto hwnd = parse_hwnd(*hwnd_s);
      if (!hwnd) throw std::runtime_error("bad hwnd");
      auto info = backend_->get_info(snapshot, *hwnd);
      if (!info) { resp.ok=false; resp.error_code="E_BAD_HWND"; resp.error_message="not a valid window handle"; return resp; }
      resp.result = window_info_to_json(*info);
      return resp;
    }

    if (req.method == "window.pickAtPoint") {
      auto x = get_num(req.params, "x");
      auto y = get_num(req.params, "y");
      if (!x || !y) throw std::runtime_error("missing x/y");
      PickFlags flags;
      if (auto b = get_bool(req.params, "prefer_child")) flags.prefer_child = *b;
      if (auto b = get_bool(req.params, "ignore_transparent")) flags.ignore_transparent = *b;
      auto h = backend_->pick_at_point(snapshot, (int)*x, (int)*y, flags);
      if (!h) { resp.ok=false; resp.error_code="E_NOT_FOUND"; resp.error_message="no window at point"; return resp; }
      json::Object o; o["hwnd"] = fmt_hwnd(*h);
      resp.result = o;
      return resp;
    }

    if (req.method == "window.ensureVisible") {
      auto hwnd_s = get_str(req.params, "hwnd");
      auto vis = get_bool(req.params, "visible");
      if (!hwnd_s || !vis) throw std::runtime_error("missing hwnd/visible");
      auto hwnd = parse_hwnd(*hwnd_s);
      if (!hwnd) throw std::runtime_error("bad hwnd");
      auto r = backend_->ensure_visible(*hwnd, *vis);
      json::Object o; o["changed"] = r.changed;
      resp.result = o;
      return resp;
    }

    if (req.method == "window.ensureForeground") {
      auto hwnd_s = get_str(req.params, "hwnd");
      if (!hwnd_s) throw std::runtime_error("missing hwnd");
      auto hwnd = parse_hwnd(*hwnd_s);
      if (!hwnd) throw std::runtime_error("bad hwnd");
      auto r = backend_->ensure_foreground(*hwnd);
      json::Object o; o["changed"] = r.changed;
      resp.result = o;
      return resp;
    }

    if (req.method == "window.postMessage") {
      auto hwnd_s = get_str(req.params, "hwnd");
      auto msg = get_num(req.params, "msg");
      auto wparam = get_num(req.params, "wparam");
      auto lparam = get_num(req.params, "lparam");
      if (!hwnd_s || !msg) throw std::runtime_error("missing hwnd/msg");
      auto hwnd = parse_hwnd(*hwnd_s);
      if (!hwnd) throw std::runtime_error("bad hwnd");
      bool ok = backend_->post_message(*hwnd, (uint32_t)*msg, (uint64_t)(wparam.value_or(0)), (uint64_t)(lparam.value_or(0)));
      json::Object o; o["sent"] = ok;
      resp.result = o;
      return resp;
    }

    if (req.method == "input.send") {
      auto data_b64 = get_str(req.params, "data_b64");
      if (!data_b64) throw std::runtime_error("missing data_b64");
      auto data = base64_decode(*data_b64);
      bool ok = backend_->send_input(data);
      json::Object o; o["sent"] = ok;
      resp.result = o;
      return resp;
    }

    // snapshot.capture/events.* are handled in daemon layer (session/scoped state)
    resp.ok = false;
    resp.error_code = "E_BAD_METHOD";
    resp.error_message = "method not implemented in core";
    return resp;

  } catch (const std::exception& e) {
    resp.ok = false;
    resp.error_code = "E_BAD_REQUEST";
    resp.error_message = e.what();
    return resp;
  }
}

CoreRequest parse_request_json(std::string_view json_utf8) {
  auto v = json::parse(json_utf8);
  if (!v.is_obj()) throw std::runtime_error("request must be object");
  const auto& o = v.as_obj();

  auto it_id = o.find("id");
  auto it_m  = o.find("method");
  auto it_p  = o.find("params");
  if (it_id==o.end() || it_m==o.end() || it_p==o.end()) throw std::runtime_error("missing fields");
  if (!it_id->second.is_str() || !it_m->second.is_str() || !it_p->second.is_obj())
    throw std::runtime_error("bad field types");

  CoreRequest r;
  r.id = it_id->second.as_str();
  r.method = it_m->second.as_str();
  r.params = it_p->second.as_obj();
  return r;
}

std::string serialize_response_json(const CoreResponse& resp, bool canonical) {
  (void)canonical;
  json::Value v = resp.to_json_obj(canonical);
  return json::dumps(v);
}

} // namespace wininspect
