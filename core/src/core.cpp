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
  if (ok)
    o["result"] = result;
  else
    o["error"] = make_error(error_code, error_message);
  
  if (!metrics.empty()) {
    o["metrics"] = metrics;
  }
  return o;
}

CoreEngine::CoreEngine(IBackend *backend) : backend_(backend) {}

static std::optional<std::string> get_str(const json::Object &o,
                                          const std::string &k) {
  auto it = o.find(k);
  if (it == o.end())
    return std::nullopt;
  if (!it->second.is_str())
    return std::nullopt;
  return it->second.as_str();
}
static std::optional<bool> get_bool(const json::Object &o,
                                    const std::string &k) {
  auto it = o.find(k);
  if (it == o.end())
    return std::nullopt;
  if (!it->second.is_bool())
    return std::nullopt;
  return it->second.as_bool();
}
static std::optional<double> get_num(const json::Object &o,
                                     const std::string &k) {
  auto it = o.find(k);
  if (it == o.end())
    return std::nullopt;
  if (!it->second.is_num())
    return std::nullopt;
  return it->second.as_num();
}

static std::optional<hwnd_u64> parse_hwnd(const std::string &s) {
  if (s.rfind("0x", 0) != 0)
    return std::nullopt;
  std::uint64_t v = 0;
  std::stringstream ss;
  ss << std::hex << s.substr(2);
  ss >> v;
  if (ss.fail())
    return std::nullopt;
  return (hwnd_u64)v;
}

static json::Object event_to_json(const Event &e) {
  json::Object o;
  o["type"] = e.type;
  o["hwnd"] = Hwnd(e.hwnd).to_string();
  if (!e.property.empty())
    o["property"] = e.property;
  return o;
}

static json::Object window_node_to_json(const WindowNode &n) {
  json::Object o;
  o["hwnd"] = Hwnd(n.hwnd).to_string();
  o["title"] = n.title;
  o["class_name"] = n.class_name;
  if (!n.children.empty()) {
    json::Array arr;
    for (const auto &c : n.children)
      arr.push_back(window_node_to_json(c));
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

  json::Object wr;
  wr["left"] = (double)wi.window_rect.left;
  wr["top"] = (double)wi.window_rect.top;
  wr["right"] = (double)wi.window_rect.right;
  wr["bottom"] = (double)wi.window_rect.bottom;
  o["window_rect"] = wr;

  json::Object cr;
  cr["left"] = (double)wi.client_rect.left;
  cr["top"] = (double)wi.client_rect.top;
  cr["right"] = (double)wi.client_rect.right;
  cr["bottom"] = (double)wi.client_rect.bottom;
  o["client_rect"] = cr;

  json::Object sr;
  sr["left"] = (double)wi.screen_rect.left;
  sr["top"] = (double)wi.screen_rect.top;
  sr["right"] = (double)wi.screen_rect.right;
  sr["bottom"] = (double)wi.screen_rect.bottom;
  o["screen_rect"] = sr;

  o["pid"] = (double)wi.pid;
  o["tid"] = (double)wi.tid;

  o["style"] = Hwnd(wi.style).to_string();
  o["exstyle"] = Hwnd(wi.exstyle).to_string();

  json::Array sf;
  for (const auto &s : wi.style_flags) sf.push_back(s);
  o["style_flags"] = sf;

  json::Array esf;
  for (const auto &s : wi.ex_style_flags) esf.push_back(s);
  o["ex_style_flags"] = esf;

  o["visible"] = wi.visible;
  o["enabled"] = wi.enabled;
  o["iconic"] = wi.iconic;
  o["zoomed"] = wi.zoomed;

  o["process_image"] = wi.process_image;
  return o;
}

static std::vector<uint8_t> base64_decode(std::string_view in) {
  static const std::string_view b64 =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::vector<uint8_t> out;
  std::vector<int> T(256, -1);
  for (int i = 0; i < 64; i++)
    T[b64[i]] = i;

  int val = 0, valb = -8;
  for (char c : in) {
    if (T[c] == -1)
      break;
    val = (val << 6) + T[c];
    valb += 6;
    if (valb >= 0) {
      out.push_back(uint8_t((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return out;
}

static std::string base64_encode(const std::vector<uint8_t> &in) {
  static const char b64[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  int val = 0, valb = -6;
  for (uint8_t c : in) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      out.push_back(b64[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }
  if (valb > -6)
    out.push_back(b64[((val << 8) >> (valb + 8)) & 0x3F]);
  while (out.size() % 4)
    out.push_back('=');
  return out;
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
    for (const auto &c : el.children)
      arr.push_back(ui_element_to_json(c));
    o["children"] = arr;
  }
  return o;
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
    if (req.method == "events.poll") {
      // ... existing events.poll logic ...
      if (!old_snapshot)
        throw std::runtime_error("events.poll requires two snapshots");
      
      auto wait_ms = get_num(req.params, "wait_ms").value_or(0);
      auto interval_ms = get_num(req.params, "interval_ms").value_or(100);
      auto start = std::chrono::steady_clock::now();
      
      while (true) {
        auto events = backend_->poll_events(*old_snapshot, snapshot);
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();

        if (!events.empty() || wait_ms == 0 || elapsed >= (long long)wait_ms) {
          json::Array arr;
          for (const auto &e : events)
            arr.push_back(event_to_json(e));
          resp.result = arr;
          return resp;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds((int)interval_ms));
        break; 
      }
      resp.result = json::Array{};
      return resp;
    }

    if (req.method == "window.listTop") {
      auto top = backend_->list_top(snapshot);
      json::Array arr;
      for (auto h : top) {
        json::Object e;
        e["hwnd"] = Hwnd(h).to_string();
        arr.emplace_back(e);
      }
      resp.result = arr;
      return resp;
    }

    if (req.method == "window.listChildren") {
      auto hwnd_s = get_str(req.params, "hwnd");
      if (!hwnd_s)
        throw std::runtime_error("missing hwnd");
      auto hwnd = parse_hwnd(*hwnd_s);
      if (!hwnd)
        throw std::runtime_error("bad hwnd");
      auto ch = backend_->list_children(snapshot, *hwnd);
      json::Array arr;
      for (auto h : ch) {
        json::Object e;
        e["hwnd"] = Hwnd(h).to_string();
        arr.emplace_back(e);
      }
      resp.result = arr;
      return resp;
    }

    if (req.method == "window.getTree") {
      auto hwnd_s = get_str(req.params, "hwnd");
      hwnd_u64 root = 0;
      if (hwnd_s) {
        auto h = parse_hwnd(*hwnd_s);
        if (!h) throw std::runtime_error("bad hwnd");
        root = *h;
      }
      auto nodes = backend_->get_window_tree(snapshot, root);
      json::Array arr;
      for (const auto &n : nodes)
        arr.push_back(window_node_to_json(n));
      resp.result = arr;
      return resp;
    }

    if (req.method == "window.highlight") {
      auto hwnd_s = get_str(req.params, "hwnd");
      if (!hwnd_s) throw std::runtime_error("missing hwnd");
      auto hwnd = parse_hwnd(*hwnd_s);
      if (!hwnd) throw std::runtime_error("bad hwnd");
      bool ok = backend_->highlight_window(*hwnd);
      json::Object o;
      o["highlighted"] = ok;
      resp.result = o;
      return resp;
    }

    if (req.method == "window.getInfo") {
      auto hwnd_s = get_str(req.params, "hwnd");
      if (!hwnd_s)
        throw std::runtime_error("missing hwnd");
      auto hwnd = parse_hwnd(*hwnd_s);
      if (!hwnd)
        throw std::runtime_error("bad hwnd");
      auto info = backend_->get_info(snapshot, *hwnd);
      if (!info) {
        resp.ok = false;
        resp.error_code = "E_BAD_HWND";
        resp.error_message = "not a valid window handle";
        return resp;
      }
      resp.result = window_info_to_json(*info);
      return resp;
    }

    if (req.method == "window.pickAtPoint") {
      auto x = get_num(req.params, "x");
      auto y = get_num(req.params, "y");
      if (!x || !y)
        throw std::runtime_error("missing x/y");
      PickFlags flags;
      if (auto b = get_bool(req.params, "prefer_child"))
        flags.prefer_child = *b;
      if (auto b = get_bool(req.params, "ignore_transparent"))
        flags.ignore_transparent = *b;
      auto h = backend_->pick_at_point(snapshot, (int)*x, (int)*y, flags);
      if (!h) {
        resp.ok = false;
        resp.error_code = "E_NOT_FOUND";
        resp.error_message = "no window at point";
        return resp;
      }
      json::Object o;
      o["hwnd"] = Hwnd(*h).to_string();
      resp.result = o;
      return resp;
    }

    if (req.method == "window.ensureVisible") {
      auto hwnd_s = get_str(req.params, "hwnd");
      auto vis = get_bool(req.params, "visible");
      if (!hwnd_s || !vis)
        throw std::runtime_error("missing hwnd/visible");
      auto hwnd = parse_hwnd(*hwnd_s);
      if (!hwnd)
        throw std::runtime_error("bad hwnd");
      auto r = backend_->ensure_visible(*hwnd, *vis);
      json::Object o;
      o["changed"] = r.changed;
      resp.result = o;
      return resp;
    }

    if (req.method == "window.ensureForeground") {
      auto hwnd_s = get_str(req.params, "hwnd");
      if (!hwnd_s)
        throw std::runtime_error("missing hwnd");
      auto hwnd = parse_hwnd(*hwnd_s);
      if (!hwnd)
        throw std::runtime_error("bad hwnd");
      auto r = backend_->ensure_foreground(*hwnd);
      json::Object o;
      o["changed"] = r.changed;
      resp.result = o;
      return resp;
    }

    if (req.method == "window.setProperty") {
      auto hwnd_s = get_str(req.params, "hwnd");
      auto name = get_str(req.params, "name");
      auto val = get_str(req.params, "value");
      if (!hwnd_s || !name || !val)
        throw std::runtime_error("missing hwnd/name/value");
      auto hwnd = parse_hwnd(*hwnd_s);
      if (!hwnd) throw std::runtime_error("bad hwnd");
      bool ok = backend_->set_property(*hwnd, *name, *val);
      json::Object o; o["ok"] = ok;
      resp.result = o;
      return resp;
    }

    if (req.method == "window.controlClick") {
      auto hwnd_s = get_str(req.params, "hwnd");
      auto x = get_num(req.params, "x");
      auto y = get_num(req.params, "y");
      auto btn = get_num(req.params, "button");
      if (!hwnd_s || !x || !y)
        throw std::runtime_error("missing hwnd/x/y");
      auto hwnd = parse_hwnd(*hwnd_s);
      if (!hwnd) throw std::runtime_error("bad hwnd");
      bool ok = backend_->control_click(*hwnd, (int)*x, (int)*y, (int)btn.value_or(0));
      json::Object o; o["sent"] = ok;
      resp.result = o;
      return resp;
    }

    if (req.method == "window.controlSend") {
      auto hwnd_s = get_str(req.params, "hwnd");
      auto text = get_str(req.params, "text");
      if (!hwnd_s || !text)
        throw std::runtime_error("missing hwnd/text");
      auto hwnd = parse_hwnd(*hwnd_s);
      if (!hwnd) throw std::runtime_error("bad hwnd");
      bool ok = backend_->control_send(*hwnd, *text);
      json::Object o; o["sent"] = ok;
      resp.result = o;
      return resp;
    }

    if (req.method == "screen.pixelSearch") {
      auto left = get_num(req.params, "left");
      auto top = get_num(req.params, "top");
      auto right = get_num(req.params, "right");
      auto bottom = get_num(req.params, "bottom");
      auto r_val = get_num(req.params, "r");
      auto g_val = get_num(req.params, "g");
      auto b_val = get_num(req.params, "b");
      auto var = get_num(req.params, "variation").value_or(0);

      if (!left || !top || !right || !bottom || !r_val || !g_val || !b_val)
        throw std::runtime_error("missing parameters");

      Rect region{(long)*left, (long)*top, (long)*right, (long)*bottom};
      Color target{(uint8_t)*r_val, (uint8_t)*g_val, (uint8_t)*b_val};
      
      auto res = backend_->pixel_search(region, target, (int)var);
      if (res) {
        json::Object o;
        o["x"] = (double)res->first;
        o["y"] = (double)res->second;
        resp.result = o;
      } else {
        resp.ok = false;
        resp.error_code = "E_NOT_FOUND";
        resp.error_message = "color not found in region";
      }
      return resp;
    }

    if (req.method == "process.list") {
      auto procs = backend_->list_processes();
      json::Array arr;
      for (const auto &p : procs) {
        json::Object o;
        o["pid"] = (double)p.pid;
        o["name"] = p.name;
        o["path"] = p.path;
        arr.push_back(o);
      }
      resp.result = arr;
      return resp;
    }

    if (req.method == "process.kill") {
      auto pid = get_num(req.params, "pid");
      if (!pid) throw std::runtime_error("missing pid");
      bool ok = backend_->kill_process((uint32_t)*pid);
      json::Object o; o["ok"] = ok;
      resp.result = o;
      return resp;
    }

    if (req.method == "file.getInfo") {
      auto path = get_str(req.params, "path");
      if (!path) throw std::runtime_error("missing path");
      auto fi = backend_->get_file_info(*path);
      if (!fi) {
        resp.ok = false;
        resp.error_code = "E_NOT_FOUND";
        return resp;
      }
      json::Object o;
      o["path"] = fi->path;
      o["size"] = (double)fi->size;
      o["is_directory"] = fi->is_directory;
      resp.result = o;
      return resp;
    }

    if (req.method == "file.read") {
      auto path = get_str(req.params, "path");
      if (!path) throw std::runtime_error("missing path");
      auto content = backend_->read_file_content(*path);
      if (!content) {
        resp.ok = false;
        resp.error_code = "E_READ_FAILED";
        return resp;
      }
      json::Object o;
      o["content_b64"] = base64_encode(std::vector<uint8_t>(content->begin(), content->end()));
      resp.result = o;
      return resp;
    }

    if (req.method == "clipboard.read") {
      auto text = backend_->clipboard_read();
      json::Object o;
      if (text) o["text"] = *text;
      resp.result = o;
      return resp;
    }

    if (req.method == "clipboard.write") {
      auto text = get_str(req.params, "text");
      if (!text) throw std::runtime_error("missing text");
      bool ok = backend_->clipboard_write(*text);
      json::Object o; o["ok"] = ok;
      resp.result = o;
      return resp;
    }

    if (req.method == "service.list") {
      auto svcs = backend_->service_list();
      json::Array arr;
      for (const auto &s : svcs) {
        json::Object o;
        o["name"] = s.name;
        o["display_name"] = s.display_name;
        o["state"] = s.state;
        arr.push_back(o);
      }
      resp.result = arr;
      return resp;
    }

    if (req.method == "service.status") {
      auto name = get_str(req.params, "name");
      if (!name) throw std::runtime_error("missing name");
      json::Object o;
      o["status"] = backend_->service_status(*name);
      resp.result = o;
      return resp;
    }

    if (req.method == "service.control") {
      auto name = get_str(req.params, "name");
      auto action = get_str(req.params, "action");
      if (!name || !action) throw std::runtime_error("missing name/action");
      bool ok = backend_->service_control(*name, *action);
      json::Object o; o["ok"] = ok;
      resp.result = o;
      return resp;
    }

    if (req.method == "env.get") {
      auto vars = backend_->env_get_all();
      json::Object o;
      for (const auto &v : vars) o[v.name] = v.value;
      resp.result = o;
      return resp;
    }

    if (req.method == "env.set") {
      auto name = get_str(req.params, "name");
      auto val = get_str(req.params, "value");
      if (!name || !val) throw std::runtime_error("missing name/value");
      bool ok = backend_->env_set(*name, *val);
      json::Object o; o["ok"] = ok;
      resp.result = o;
      return resp;
    }

    if (req.method == "wine.drives") {
      auto drives = backend_->wine_get_drives();
      json::Array arr;
      for (const auto &d : drives) {
        json::Object o;
        o["letter"] = d.letter;
        o["mapping"] = d.mapping;
        o["type"] = d.type;
        arr.push_back(o);
      }
      resp.result = arr;
      return resp;
    }

    if (req.method == "wine.overrides") {
      auto ovr = backend_->wine_get_overrides();
      json::Array arr;
      for (const auto &s : ovr) arr.push_back(s);
      resp.result = arr;
      return resp;
    }

    if (req.method == "sync.checkMutex") {
      auto name = get_str(req.params, "name");
      if (!name) throw std::runtime_error("missing name");
      bool exists = backend_->sync_check_mutex(*name);
      json::Object o; o["exists"] = exists;
      resp.result = o;
      return resp;
    }

    if (req.method == "sync.createMutex") {
      auto name = get_str(req.params, "name");
      auto own = get_bool(req.params, "own").value_or(true);
      if (!name) throw std::runtime_error("missing name");
      bool created = backend_->sync_create_mutex(*name, own);
      json::Object o; o["created"] = created;
      resp.result = o;
      return resp;
    }

    if (req.method == "mem.read") {
      auto pid = get_num(req.params, "pid");
      auto addr = get_num(req.params, "address");
      auto size = get_num(req.params, "size");
      if (!pid || !addr || !size) throw std::runtime_error("missing parameters");
      auto res = backend_->mem_read((uint32_t)*pid, (uint64_t)*addr, (size_t)*size);
      if (!res) { resp.ok = false; return resp; }
      json::Object o;
      o["address"] = (double)res->address;
      o["data_b64"] = res->data_b64;
      resp.result = o;
      return resp;
    }

    if (req.method == "mem.write") {
      auto pid = get_num(req.params, "pid");
      auto addr = get_num(req.params, "address");
      auto data_b64 = get_str(req.params, "data_b64");
      if (!pid || !addr || !data_b64) throw std::runtime_error("missing parameters");
      auto data = base64_decode(*data_b64);
      bool ok = backend_->mem_write((uint32_t)*pid, (uint64_t)*addr, data);
      json::Object o; o["ok"] = ok;
      resp.result = o;
      return resp;
    }

    if (req.method == "image.match") {
      auto left = get_num(req.params, "left");
      auto top = get_num(req.params, "top");
      auto right = get_num(req.params, "right");
      auto bottom = get_num(req.params, "bottom");
      auto sub_b64 = get_str(req.params, "sub_image_b64");
      if (!left || !top || !right || !bottom || !sub_b64) throw std::runtime_error("missing parameters");
      
      Rect r{(long)*left, (long)*top, (long)*right, (long)*bottom};
      auto sub = base64_decode(*sub_b64);
      auto res = backend_->image_match(r, sub);
      if (!res) { resp.ok = false; return resp; }
      json::Object o;
      o["x"] = (double)res->x;
      o["y"] = (double)res->y;
      o["confidence"] = res->confidence;
      resp.result = o;
      return resp;
    }

    if (req.method == "input.hook") {
      auto enabled = get_bool(req.params, "enabled");
      if (!enabled) throw std::runtime_error("missing enabled");
      bool ok = backend_->input_hook_enable(*enabled);
      json::Object o; o["ok"] = ok;
      resp.result = o;
      return resp;
    }

    if (req.method == "window.findRegex") {
      auto t_re = get_str(req.params, "title_regex").value_or(".*");
      auto c_re = get_str(req.params, "class_regex").value_or(".*");
      auto hwnds = backend_->find_windows_regex(t_re, c_re);
      json::Array arr;
      for (auto h : hwnds) {
        json::Object e; e["hwnd"] = Hwnd(h).to_string();
        arr.push_back(e);
      }
      resp.result = arr;
      return resp;
    }

    if (req.method == "reg.read") {
      auto path = get_str(req.params, "path");
      if (!path) throw std::runtime_error("missing path");
      auto res = backend_->reg_read(*path);
      if (!res) {
        resp.ok = false;
        resp.error_code = "E_NOT_FOUND";
        return resp;
      }
      json::Object o;
      o["path"] = res->path;
      json::Array sk;
      for (const auto &s : res->subkeys) sk.push_back(s);
      o["subkeys"] = sk;
      json::Array vals;
      for (const auto &v : res->values) {
        json::Object vo;
        vo["name"] = v.name;
        vo["type"] = v.type;
        vo["data"] = v.data;
        vals.push_back(vo);
      }
      o["values"] = vals;
      resp.result = o;
      return resp;
    }

    if (req.method == "reg.write") {
      auto path = get_str(req.params, "path");
      auto name = get_str(req.params, "name");
      auto type = get_str(req.params, "type");
      auto data = get_str(req.params, "data");
      if (!path || !name || !type || !data) throw std::runtime_error("missing parameters");
      
      RegistryValue rv;
      rv.name = *name;
      rv.type = *type;
      rv.data = *data;
      
      bool ok = backend_->reg_write(*path, rv);
      json::Object o; o["ok"] = ok;
      resp.result = o;
      return resp;
    }

    if (req.method == "reg.delete") {
      auto path = get_str(req.params, "path");
      auto name = get_str(req.params, "name").value_or("");
      if (!path) throw std::runtime_error("missing path");
      bool ok = backend_->reg_delete(*path, name);
      json::Object o; o["ok"] = ok;
      resp.result = o;
      return resp;
    }

    if (req.method == "screen.getPixel") {
      auto x = get_num(req.params, "x");
      auto y = get_num(req.params, "y");
      if (!x || !y) throw std::runtime_error("missing x/y");
      auto c = backend_->get_pixel((int)*x, (int)*y);
      if (!c) throw std::runtime_error("failed to get pixel");
      json::Object o;
      o["hex"] = c->to_hex();
      o["r"] = (double)c->r;
      o["g"] = (double)c->g;
      o["b"] = (double)c->b;
      resp.result = o;
      return resp;
    }

    if (req.method == "screen.capture") {
      auto left = get_num(req.params, "left");
      auto top = get_num(req.params, "top");
      auto right = get_num(req.params, "right");
      auto bottom = get_num(req.params, "bottom");
      if (!left || !top || !right || !bottom)
        throw std::runtime_error("missing region");
      Rect r{(long)*left, (long)*top, (long)*right, (long)*bottom};
      auto sc = backend_->capture_screen(r);
      if (!sc) throw std::runtime_error("capture failed");
      json::Object o;
      o["width"] = (double)sc->width;
      o["height"] = (double)sc->height;
      o["data_b64"] = sc->data_b64;
      resp.result = o;
      return resp;
    }

    if (req.method == "screen.capture") {
      auto left = get_num(req.params, "left");
      auto top = get_num(req.params, "top");
      auto right = get_num(req.params, "right");
      auto bottom = get_num(req.params, "bottom");
      if (!left || !top || !right || !bottom)
        throw std::runtime_error("missing region");
      Rect r{(long)*left, (long)*top, (long)*right, (long)*bottom};
      auto sc = backend_->capture_screen(r);
      if (!sc) throw std::runtime_error("capture failed");
      json::Object o;
      o["width"] = (double)sc->width;
      o["height"] = (double)sc->height;
      o["data_b64"] = sc->data_b64;
      resp.result = o;
      return resp;
    }

    if (req.method == "window.postMessage") {
      auto hwnd_s = get_str(req.params, "hwnd");
      auto msg = get_num(req.params, "msg");
      auto wparam = get_num(req.params, "wparam");
      auto lparam = get_num(req.params, "lparam");
      if (!hwnd_s || !msg)
        throw std::runtime_error("missing hwnd/msg");
      auto hwnd = parse_hwnd(*hwnd_s);
      if (!hwnd)
        throw std::runtime_error("bad hwnd");
      bool ok = backend_->post_message(*hwnd, (uint32_t)*msg,
                                       (uint64_t)(wparam.value_or(0)),
                                       (uint64_t)(lparam.value_or(0)));
      json::Object o;
      o["sent"] = ok;
      resp.result = o;
      return resp;
    }

    if (req.method == "input.send") {
      auto data_b64 = get_str(req.params, "data_b64");
      if (!data_b64)
        throw std::runtime_error("missing data_b64");
      auto data = base64_decode(*data_b64);
      bool ok = backend_->send_input(data);
      json::Object o;
      o["sent"] = ok;
      resp.result = o;
      return resp;
    }

    if (req.method == "input.mouseClick") {
      auto x = get_num(req.params, "x");
      auto y = get_num(req.params, "y");
      auto btn = get_num(req.params, "button"); // 0=left, 1=right, 2=middle
      if (!x || !y)
        throw std::runtime_error("missing x/y");
      int b = (int)btn.value_or(0);
      bool ok = backend_->send_mouse_click((int)*x, (int)*y, b);
      json::Object o;
      o["sent"] = ok;
      resp.result = o;
      return resp;
    }

    if (req.method == "input.keyPress") {
      auto vk = get_num(req.params, "vk");
      if (!vk)
        throw std::runtime_error("missing vk");
      bool ok = backend_->send_key_press((int)*vk);
      json::Object o;
      o["sent"] = ok;
      resp.result = o;
      return resp;
    }

    if (req.method == "input.text") {
      auto text = get_str(req.params, "text");
      if (!text)
        throw std::runtime_error("missing text");
      bool ok = backend_->send_text(*text);
      json::Object o;
      o["sent"] = ok;
      resp.result = o;
      return resp;
    }

    if (req.method == "ui.inspect") {
      auto hwnd_s = get_str(req.params, "hwnd");
      if (!hwnd_s)
        throw std::runtime_error("missing hwnd");
      auto hwnd = parse_hwnd(*hwnd_s);
      if (!hwnd)
        throw std::runtime_error("bad hwnd");

      auto elements = backend_->inspect_ui_elements(*hwnd);
      json::Array arr;
      for (const auto &el : elements) {
        arr.push_back(ui_element_to_json(el));
      }
      resp.result = arr;
      return resp;
    }

    if (req.method == "ui.invoke") {
      auto hwnd_s = get_str(req.params, "hwnd");
      auto aid = get_str(req.params, "automation_id");
      if (!hwnd_s || !aid)
        throw std::runtime_error("missing hwnd/automation_id");
      auto hwnd = parse_hwnd(*hwnd_s);
      if (!hwnd)
        throw std::runtime_error("bad hwnd");

      bool ok = backend_->invoke_ui_element(*hwnd, *aid);
      json::Object o;
      o["invoked"] = ok;
      resp.result = o;
      return resp;
    }

    if (req.method == "daemon.health") {
      resp.result = backend_->get_env_metadata();
      return resp;
    }

    if (req.method == "daemon.logs") {
      auto logs = Logger::get().get_recent_logs();
      json::Array arr;
      for (const auto &l : logs) {
        json::Object lo;
        lo["timestamp"] = l.timestamp;
        lo["level"] = (double)static_cast<int>(l.level);
        lo["message"] = l.message;
        arr.push_back(lo);
      }
      resp.result = arr;
      return resp;
    }

    // snapshot.capture/events.* are handled in daemon layer (session/scoped
    // state)
    resp.ok = false;
    resp.error_code = "E_BAD_METHOD";
    resp.error_message = "method not implemented in core";
    LOG_WARN("Method not implemented: " + req.method);

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
  if (!v.is_obj())
    throw std::runtime_error("request must be object");
  const auto &o = v.as_obj();

  auto it_id = o.find("id");
  auto it_m = o.find("method");
  auto it_p = o.find("params");
  if (it_id == o.end() || it_m == o.end() || it_p == o.end())
    throw std::runtime_error("missing fields");
  if (!it_id->second.is_str() || !it_m->second.is_str() ||
      !it_p->second.is_obj())
    throw std::runtime_error("bad field types");

  CoreRequest r;
  r.id = it_id->second.as_str();
  r.method = it_m->second.as_str();
  r.params = it_p->second.as_obj();
  return r;
}

std::string serialize_response_json(const CoreResponse &resp, bool canonical) {
  (void)canonical;
  json::Value v = resp.to_json_obj(canonical);
  return json::dumps(v);
}

} // namespace wininspect
