// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

// Lightweight HTTP server for REST API access to the daemon.
//
// Usage: wininspectd --http-port 8080 [--http-token secret]
//
// Endpoints:
//   GET    /api/v1/health       → daemon.health
//   GET    /api/v1/identity     → daemon.identity
//   GET    /api/v1/capabilities → daemon.capabilities
//   POST   /api/v1/capture      → screen.capture
//   GET    /api/v1/windows      → window.listTop
//   POST   /api/v1/click        → input.mouseClick
//   POST   /api/v1/type         → input.text
//   POST   /api/v1/hotkey       → input.hotkey
//   GET    /api/v1/processes    → process.list
//   POST   /api/v1/exec         → process.execute

#include "wininspect/core.hpp"
#include "wininspect/logger.hpp"
#include "wininspect/tinyjson.hpp"
#include <fstream>

// Embedded WebUI dashboard (served at /dashboard)
static const char *DASHBOARD_HTML = R"raw(<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>WinInspect Dashboard</title>
<style>
*{margin:0;padding:0;box-sizing:border-box;font-family:-apple-system,sans-serif}
body{background:#1a1a2e;color:#e0e0e0;padding:20px}
h1{color:#00d4aa;margin-bottom:20px;font-size:24px}
h2{color:#00d4aa;margin:20px 0 10px;font-size:18px}
.card{background:#16213e;border-radius:8px;padding:15px;margin-bottom:15px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:15px}
label{display:block;margin:8px 0 4px;color:#888;font-size:12px}
input,select,button{width:100%;padding:8px;border:1px solid #0f3460;border-radius:4px;background:#0f3460;color:#e0e0e0;font-size:14px}
button{background:#00d4aa;color:#1a1a2e;font-weight:bold;cursor:pointer;margin-top:8px}
button:hover{background:#00f5c0}
pre{background:#0f3460;padding:10px;border-radius:4px;overflow:auto;font-size:12px;max-height:300px}
img{max-width:100%;border-radius:4px}
.row{display:flex;gap:8px}.row input{flex:1}.row button{flex:0;width:auto;padding:8px 16px}
</style></head><body>
<h1>WinInspect Dashboard</h1>
<div class=grid><div>
<div class=card><h2>Connection</h2><label>Daemon URL</label><div class=row><input id=url value=http://localhost:8088><button onclick=connect()>Connect</button></div><div id=status style=margin-top:8px;color:#888>Disconnected</div></div>
<div class=card><h2>Identity</h2><pre id=identity></pre></div></div><div>
<div class=card><h2>Actions</h2>
<label>Click X,Y</label><div class=row><input id=cx placeholder=X value=500><input id=cy placeholder=Y value=500><button onclick=doClick()>Click</button></div>
<label>Type text</label><div class=row><input id=txt placeholder=text><button onclick=doType()>Type</button></div>
<label>Hotkey</label><div class=row><input id=hk placeholder="Ctrl+C"><button onclick=doHotkey()>Send</button></div>
</div></div></div>
<div class=card><h2>Screen</h2><button onclick=capture()>Capture</button><img id=ss style=display:none;margin-top:10px></div>
<div class=card><h2>Windows</h2><button onclick=listWin()>Refresh</button><pre id=wlist></pre></div>
<script>
let BASE='http://localhost:8088';
async function api(p,m,b){let o={method:m||'GET',headers:{'Content-Type':'application/json'}};if(b)o.body=JSON.stringify(b);return(await fetch(BASE+p,o)).json()}
async function connect(){BASE=document.getElementById('url').value.replace(/\/+$/,'');document.getElementById('status').textContent='Connecting...';try{let h=await api('/api/v1/health');let i=await api('/api/v1/identity');document.getElementById('status').textContent='Connected '+(h.result?.os||'');document.getElementById('identity').textContent=JSON.stringify(i.result||i,null,2)}catch(e){document.getElementById('status').textContent='Error: '+e.message}}
async function capture(){try{let r=await api('/api/v1/capture','POST',{left:0,top:0,right:1920,bottom:1080});let d=r.result?.data_b64;if(d){document.getElementById('ss').src='data:image/bmp;base64,'+d;document.getElementById('ss').style.display='block'}}catch(e){alert(e.message)}}
async function doClick(){await api('/api/v1/click','POST',{x:+document.getElementById('cx').value,y:+document.getElementById('cy').value})}
async function doType(){let t=document.getElementById('txt').value;if(t)await api('/api/v1/type','POST',{text:t})}
async function doHotkey(){let k=document.getElementById('hk').value;if(k)await api('/api/v1/hotkey','POST',{keys:k})}
async function listWin(){let r=await api('/api/v1/windows');document.getElementById('wlist').textContent=JSON.stringify(r.result||r,null,2)}
</script></body></html>)raw";

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

#include <string>
#include <map>
#include <functional>
#include <sstream>
#include <atomic>
#include <thread>
#include <cstring>

using namespace wininspect;

namespace wininspectd {

// ── HTTP Types ──────────────────────────────────────────────────────────────

struct HttpReq {
  std::string method, path, body;
  std::map<std::string, std::string> headers;
};

struct HttpResp {
  int code = 200;
  std::string status = "OK", body;
  std::string content_type = "application/json; charset=utf-8";
};

static std::string build_response(const HttpResp &r) {
  std::ostringstream ss;
  ss << "HTTP/1.1 " << r.code << " " << r.status << "\r\n"
     << "Content-Type: " << r.content_type << "\r\n"
     << "Content-Length: " << r.body.size() << "\r\n"
     << "Connection: close\r\n"
     << "Access-Control-Allow-Origin: *\r\n"
     << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
     << "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
     << "\r\n" << r.body;
  return ss.str();
}

static bool parse_http(const std::string &raw, HttpReq &req) {
  auto eol = raw.find("\r\n");
  if (eol == std::string::npos) return false;
  auto fl = raw.substr(0, eol);
  auto s1 = fl.find(' '), s2 = fl.rfind(' ');
  if (s1 == std::string::npos || s2 == std::string::npos || s1 == s2) return false;
  req.method = fl.substr(0, s1);
  req.path = fl.substr(s1 + 1, s2 - s1 - 1);
  size_t pos = eol + 2;
  while (pos < raw.size()) {
    auto he = raw.find("\r\n", pos);
    if (he == std::string::npos) break;
    if (he == pos) { pos = he + 2; break; }
    auto c = raw.find(':', pos);
    if (c != std::string::npos && c < he) {
      req.headers[raw.substr(pos, c - pos)] = raw.substr(c + 2, he - c - 2);
    }
    pos = he + 2;
  }
  if (pos < raw.size()) req.body = raw.substr(pos);
  return true;
}

// ── Route Table ─────────────────────────────────────────────────────────────

struct Route {
  const char *method;
  const char *path;
  const char *rpc_method;  // core dispatch method
  std::function<void(const HttpReq&, json::Object&)> param_fill;
};

static json::Object json_from(const HttpReq &req) {
  if (req.body.empty()) return json::Object{};
  try {
    auto v = json::parse(req.body);
    if (v.is_obj()) return v.as_obj();
  } catch (...) {}
  return json::Object{};
}

// ── HTTP Server ─────────────────────────────────────────────────────────────

void run_http_server(std::atomic<bool> *running, int port,
                      CoreEngine &core, const std::string &auth_token) {
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) { LOG_ERROR("HTTP: Winsock init failed"); return; }

  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) { WSACleanup(); return; }

  int on = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons((u_short)port);

  if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
    LOG_ERROR("HTTP: bind failed on port " + std::to_string(port));
    closesocket(s); WSACleanup(); return;
  }
  listen(s, SOMAXCONN);

  u_long mode = 1;
  ioctlsocket(s, FIONBIO, &mode);

  auto snapshot = core.get_backend()->capture_snapshot();

  // Route table: method, path, rpc_method, param_filler
  Route routes[] = {
    {"GET", "/api/v1/health", "daemon.health", nullptr},
    {"GET", "/api/v1/identity", "daemon.identity", nullptr},
    {"GET", "/api/v1/capabilities", "daemon.capabilities", nullptr},
    {"POST", "/api/v1/capture", "screen.capture", nullptr},
    {"GET", "/api/v1/windows", "window.listTop", nullptr},
    {"POST", "/api/v1/click", "input.mouseClick", [](const HttpReq &r, json::Object &p) {
      auto j = json_from(r);
      auto it = j.find("x"); if (it != j.end()) p["x"] = it->second;
      it = j.find("y"); if (it != j.end()) p["y"] = it->second;
      it = j.find("button"); if (it != j.end()) p["button"] = it->second;
    }},
    {"POST", "/api/v1/type", "input.text", [](const HttpReq &r, json::Object &p) {
      auto j = json_from(r);
      auto it = j.find("text"); if (it != j.end()) p["text"] = it->second;
    }},
    {"POST", "/api/v1/hotkey", "input.hotkey", [](const HttpReq &r, json::Object &p) {
      auto j = json_from(r);
      auto it = j.find("keys"); if (it != j.end()) p["keys"] = it->second;
    }},
    {"GET", "/api/v1/processes", "process.list", nullptr},
    {"POST", "/api/v1/exec", "process.execute", [](const HttpReq &r, json::Object &p) {
      auto j = json_from(r);
      auto it = j.find("command"); if (it != j.end()) p["command"] = it->second;
      it = j.find("args"); if (it != j.end()) p["args"] = it->second;
    }},
  };

  LOG_INFO("HTTP server listening on port " + std::to_string(port));

  while (running->load()) {
    SOCKET c = accept(s, nullptr, nullptr);
    if (c == INVALID_SOCKET) { Sleep(100); continue; }

    char buf[8192];
    int r = recv(c, buf, sizeof(buf) - 1, 0);
    if (r <= 0) { closesocket(c); continue; }
    buf[r] = '\0';

    HttpReq req;
    HttpResp resp;

    if (!parse_http(std::string(buf), req)) {
      resp.code = 400; resp.status = "Bad Request"; resp.body = R"({"error":"bad request"})";
      std::string out = build_response(resp);
      send(c, out.data(), (int)out.size(), 0);
      closesocket(c); continue;
    }

    // CORS preflight
    if (req.method == "OPTIONS") {
      resp.code = 204;
      std::string out = build_response(resp);
      send(c, out.data(), (int)out.size(), 0);
      closesocket(c); continue;
    }

    // Auth check
    if (!auth_token.empty()) {
      auto it = req.headers.find("Authorization");
      std::string token_val;
      if (it != req.headers.end() && it->second.size() > 7 && it->second.substr(0,7) == "Bearer ")
        token_val = it->second.substr(7);
      if (token_val != auth_token) {
        resp.code = 401; resp.status = "Unauthorized"; resp.body = R"({"error":"unauthorized"})";
        std::string out = build_response(resp);
        send(c, out.data(), (int)out.size(), 0);
        closesocket(c); continue;
      }
    }

    // Find matching route
    const Route *matched = nullptr;
    for (auto &rt : routes) {
      if (req.method == rt.method && req.path == rt.path) { matched = &rt; break; }
    }

    // Serve dashboard UI (embedded HTML)
    if (req.path == "/dashboard") {
      resp.content_type = "text/html; charset=utf-8";
      resp.body = DASHBOARD_HTML;
      goto send_it;
    }
    // Redirect / to /dashboard
    if (req.path == "/") {
      resp.code = 301; resp.status = "Moved";
      resp.body = "<html><body>Redirecting to <a href='/dashboard'>dashboard</a>...</body></html>";
      resp.content_type = "text/html";
      goto send_it;
    }

    if (!matched) {
      resp.code = 404; resp.status = "Not Found"; resp.body = R"({"error":"not found"})";
      goto send_it;
    }

    // Dispatch to core engine
    try {
      json::Object params;
      if (matched->param_fill) matched->param_fill(req, params);
      CoreRequest creq{"http-1", matched->rpc_method, params};
      auto snap = core.get_backend()->capture_snapshot();
      auto cresp = core.handle(creq, snap, nullptr);

      json::Object result;
      result["ok"] = cresp.ok;
      result["id"] = cresp.id;
      result["result"] = cresp.result;
      if (!cresp.error_code.empty()) result["error_code"] = cresp.error_code;
      if (!cresp.error_message.empty()) result["error_message"] = cresp.error_message;
      resp.body = json::dumps(result);
    } catch (const std::exception &e) {
      json::Object err;
      err["ok"] = false; err["error"] = std::string(e.what());
      resp.body = json::dumps(err);
    }

  send_it:
    std::string out = build_response(resp);
    send(c, out.data(), (int)out.size(), 0);
    closesocket(c);
  }

  closesocket(s); WSACleanup();
}

} // namespace wininspectd
