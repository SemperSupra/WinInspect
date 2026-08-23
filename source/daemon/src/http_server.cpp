// SPDX-License-Identifier: PolyForm-NC-1.0.0
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
#include "wininspect/types.hpp"
#include "server_state.hpp"
#include "wininspect/tinyjson.hpp"
#include "wininspect/websocket.hpp"
#include "wininspect/compress.hpp"
#include "wininspect/tls.hpp"
#include <fstream>

// Embedded WebUI dashboard (served at /dashboard)
static const char* DASHBOARD_HTML =
    R"raw(<!DOCTYPE html><html><head><meta charset=utf-8><title>WinInspect Dashboard</title><style>*{margin:0;padding:0;box-sizing:border-box;font-family:-apple-system,sans-serif}body{background:#1a1d23;color:#e0e0e0;padding:20px}h1{color:#4fc3f7;margin:0;font-size:24px}.tagline{color:#888;font-size:13px;margin-top:2px}h2{color:#4fc3f7;margin:20px 0 10px;font-size:18px}.card{background:#1e2128;border:1px solid #2a2d35;border-radius:8px;padding:15px;margin-bottom:15px}.grid{display:grid;grid-template-columns:1fr 1fr;gap:15px}label{display:block;margin:8px 0 4px;color:#888;font-size:12px}input,select,button{width:100%;padding:8px;border:1px solid #2a2d35;border-radius:4px;background:#252830;color:#e0e0e0;font-size:14px}button{background:#1565c0;color:#fff;font-weight:bold;cursor:pointer;margin-top:8px;border:1px solid #1565c0}button:hover{background:#1976d2}pre{background:#111318;border:1px solid #2a2d35;padding:10px;border-radius:4px;overflow:auto;font-size:12px;max-height:300px}.row{display:flex;gap:8px}.row input{flex:1}.row button{flex:0;width:auto;padding:8px 16px}.strix{display:flex;align-items:center;gap:12px;margin-bottom:16px}.badge{padding:8px;border-radius:4px;font-size:18px;font-weight:bold;text-align:center;margin:4px 0}</style></head><body><div class=strix><svg xmlns="http://www.w3.org/2000/svg" width=40 height=40 viewBox="0 0 100 100"><rect x=6 y=10 width=56 height=42 rx=4 fill=none stroke=#4fc3f7 stroke-width=2 opacity=.4/><line x1=6 y1=20 x2=62 y2=20 stroke=#4fc3f7 stroke-width=2 opacity=.4/><ellipse cx=34 cy=40 rx=10 ry=12 fill=#4fc3f7/><circle cx=34 cy=30 r=8 fill=#4fc3f7/><polygon points="28,26 26,20 31,24" fill=#4fc3f7/><polygon points="40,26 42,20 37,24" fill=#4fc3f7/><circle cx=31 cy=29 r=2.5 fill=#111/><circle cx=37 cy=29 r=2.5 fill=#111/><polygon points="32,32 34,35 36,32" fill=#f9a825/></svg><div><h1>WinInspect</h1><div class=tagline>window inspection for Windows and Wine</div></div></div><div class=grid><div><div class=card><h2>Status</h2><div class=badge id=ctrlState style=background:#555>○ Connecting...</div><div id=connStatus style=margin-top:4px;color:#888;font-size:12px>Disconnected</div></div><div class=card><h2>Connection</h2><div class=row><input id=url value=http://localhost:8088><button onclick=connect()>Connect</button></div></div></div><div><div class=card><h2>Control</h2><button onclick=takeControl() style=background:#e67e22;color:#fff;margin-bottom:4px>Take Control</button><button onclick=releaseControl() style=background:#4caf50;color:#fff>Release to Agent</button></div><div class=card><h2>Actions</h2><label>Click X,Y</label><div class=row><input id=cx placeholder=X value=500><input id=cy placeholder=Y value=500><button onclick=doClick()>Click</button></div><label>Type text</label><div class=row><input id=txt placeholder=text><button onclick=doType()>Type</button></div><label>Hotkey</label><div class=row><input id=hk placeholder="Ctrl+C"><button onclick=doHotkey()>Send</button></div></div></div></div><div class=card><h2>Screen</h2><button onclick=capture()>Capture</button><img id=ss style=display:none;margin-top:10px></div><div class=card><h2>Windows</h2><button onclick=listWin()>Refresh</button><pre id=wlist></pre></div><script>let BASE="http://localhost:8088";let ws=null;async function api(p,m,b){let o={method:m||"GET",headers:{"Content-Type":"application/json"}};if(b)o.body=JSON.stringify(b);let r=await fetch(BASE+p,o);return r.json()}function connectWS(){if(ws)ws.close();let u=BASE.replace(/^http/,"ws")+"/api/v1/events";try{ws=new WebSocket(u);ws.onopen=()=>updateControl("idle","","");ws.onclose=()=>updateControl("disconnected","","")}catch(e){}}function updateControl(s,c,id){let el=document.getElementById("ctrlState");if(s==="disconnected"){el.style.background="#555";el.textContent="Disconnected"}if(c==="human"){el.style.background="#4caf50";el.textContent="Human in Control"}else if(c==="agent"||c==="script"){el.style.background="#e53935";el.textContent=c.charAt(0).toUpperCase()+c.slice(1)+" in Control"}else{el.style.background="#555";el.textContent="Idle"}}async function refreshStatus(){try{let s=await api("/api/v1/health");let i=await api("/api/v1/identity");document.getElementById("connStatus").textContent="Connected "+(s.result?.os||"")}catch(e){document.getElementById("connStatus").textContent="Disconnected"}}async function connect(){BASE=document.getElementById("url").value.replace(//+$/,"");connectWS();await refreshStatus()}async function takeControl(){try{await api("/api/health","POST",{})}catch(e){}updateControl("idle","human","dashboard")}async function releaseControl(){try{await api("/api/health","POST",{})}catch(e){}updateControl("idle","","")}async function capture(){try{let r=await api("/api/v1/capture","POST",{left:0,top:0,right:1920,bottom:1080});let d=r.result?.data_b64;if(d){document.getElementById("ss").src="data:image/bmp;base64,"+d;document.getElementById("ss").style.display="block"}}catch(e){alert(e.message)}}async function doClick(){await api("/api/v1/click","POST",{x:+document.getElementById("cx").value,y:+document.getElementById("cy").value})}async function doType(){let t=document.getElementById("txt").value;if(t)await api("/api/v1/type","POST",{text:t})}async function doHotkey(){let k=document.getElementById("hk").value;if(k)await api("/api/v1/hotkey","POST",{keys:k})}async function listWin(){let r=await api("/api/v1/windows");document.getElementById("wlist").textContent=JSON.stringify(r.result||r,null,2)}setTimeout(connect,500)</script></body></html>)raw";

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

  struct HttpReq
  {
    std::string method, path, body;
    std::map<std::string, std::string> headers;
  };

  struct HttpResp
  {
    int code = 200;
    std::string status = "OK", body;
    std::string content_type = "application/json; charset=utf-8";
  };

  static std::string build_response(const HttpResp& r)
  {
    std::ostringstream ss;
    ss << "HTTP/1.1 " << r.code << " " << r.status << "\r\n"
       << "Content-Type: " << r.content_type << "\r\n"
       << "Content-Length: " << r.body.size() << "\r\n"
       << "Connection: close\r\n"
       << "Access-Control-Allow-Origin: *\r\n"
       << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
       << "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
       << "\r\n"
       << r.body;
    return ss.str();
  }

  static bool parse_http(const std::string& raw, HttpReq& req)
  {
    auto eol = raw.find("\r\n");
    if (eol == std::string::npos)
      return false;
    auto fl = raw.substr(0, eol);
    auto s1 = fl.find(' '), s2 = fl.rfind(' ');
    if (s1 == std::string::npos || s2 == std::string::npos || s1 == s2)
      return false;
    req.method = fl.substr(0, s1);
    req.path = fl.substr(s1 + 1, s2 - s1 - 1);
    size_t pos = eol + 2;
    while (pos < raw.size()) {
      auto he = raw.find("\r\n", pos);
      if (he == std::string::npos)
        break;
      if (he == pos) {
        pos = he + 2;
        break;
      }
      auto c = raw.find(':', pos);
      if (c != std::string::npos && c < he) {
        req.headers[raw.substr(pos, c - pos)] = raw.substr(c + 2, he - c - 2);
      }
      pos = he + 2;
    }
    if (pos < raw.size())
      req.body = raw.substr(pos);
    return true;
  }

  // ── Route Table ─────────────────────────────────────────────────────────────

  struct Route
  {
    const char* method;
    const char* path;
    const char* rpc_method; // core dispatch method
    std::function<void(const HttpReq&, json::Object&)> param_fill;
  };

  static json::Object json_from(const HttpReq& req)
  {
    if (req.body.empty())
      return json::Object{};
    try {
      auto v = json::parse(req.body);
      if (v.is_obj())
        return v.as_obj();
    }
    catch (...) {
    }
    return json::Object{};
  }

  // ── HTTP Request Handler (shared by plain and TLS servers) ──────────────────

  static HttpResp handle_http_request(const HttpReq& req, CoreEngine& core,
                                      MetricsCollector* daemon_metrics, ServerState* state,
                                      const std::string& auth_token, bool read_only,
                                      bool no_clipboard, int redirect_https_port = 0)
  {
    // Route table
    static Route routes[] = {
        {"GET", "/api/v1/health", "daemon.health", nullptr},
        {"GET", "/api/v1/identity", "daemon.identity", nullptr},
        {"GET", "/api/v1/capabilities", "daemon.capabilities", nullptr},
        {"POST", "/api/v1/capture", "screen.capture", nullptr},
        {"GET", "/api/v1/windows", "window.listTop", nullptr},
        {"POST", "/api/v1/click", "input.mouseClick",
         [](const HttpReq& r, json::Object& p) {
           auto j = json_from(r);
           auto it = j.find("x");
           if (it != j.end())
             p["x"] = it->second;
           it = j.find("y");
           if (it != j.end())
             p["y"] = it->second;
           it = j.find("button");
           if (it != j.end())
             p["button"] = it->second;
         }},
        {"POST", "/api/v1/type", "input.text",
         [](const HttpReq& r, json::Object& p) {
           auto j = json_from(r);
           auto it = j.find("text");
           if (it != j.end())
             p["text"] = it->second;
         }},
        {"POST", "/api/v1/hotkey", "input.hotkey",
         [](const HttpReq& r, json::Object& p) {
           auto j = json_from(r);
           auto it = j.find("keys");
           if (it != j.end())
             p["keys"] = it->second;
         }},
        {"GET", "/api/v1/processes", "process.list", nullptr},
        {"POST", "/api/v1/exec", "process.execute",
         [](const HttpReq& r, json::Object& p) {
           auto j = json_from(r);
           auto it = j.find("command");
           if (it != j.end())
             p["command"] = it->second;
           it = j.find("args");
           if (it != j.end())
             p["args"] = it->second;
         }},
    };

    HttpResp resp;

    // HTTP→HTTPS redirect (if --https-port is active)
    if (redirect_https_port > 0) {
      std::string host = "localhost";
      auto hdr = req.headers.find("Host");
      if (hdr != req.headers.end())
        host = hdr->second;
      resp.code = 302;
      resp.status = "Found";
      resp.body = "<html><body>Redirecting to <a href='https://" + host + ":" +
                  std::to_string(redirect_https_port) + req.path + "'>HTTPS</a>...</body></html>";
      resp.content_type = "text/html";
      return resp;
    }

    // CORS preflight
    if (req.method == "OPTIONS") {
      resp.code = 204;
      return resp;
    }

    // Auth check
    if (!auth_token.empty()) {
      auto it = req.headers.find("Authorization");
      std::string token_val;
      if (it != req.headers.end() && it->second.size() > 7 && it->second.substr(0, 7) == "Bearer ")
        token_val = it->second.substr(7);
      if (token_val != auth_token) {
        resp.code = 401;
        resp.status = "Unauthorized";
        resp.body = R"({"error":"unauthorized"})";
        return resp;
      }
    }

    // WebSocket upgrade — handled by caller, signal with code 101
    auto conn_it = req.headers.find("Connection");
    auto upg_it = req.headers.find("Upgrade");
    if (req.path == "/api/v1/events" &&
        ws_is_upgrade(req.method, conn_it != req.headers.end() ? conn_it->second : "",
                      upg_it != req.headers.end() ? upg_it->second : "")) {
      resp.code = 101; // Switching Protocols — caller handles ws loop
      auto ws_key_it = req.headers.find("Sec-WebSocket-Key");
      std::string ws_key = ws_key_it != req.headers.end() ? ws_key_it->second : "";
      resp.body = ws_upgrade_response(ws_key);
      return resp;
    }

    // Prometheus metrics endpoint
    if (req.path == "/metrics" && req.method == "GET") {
      resp.content_type = "text/plain; charset=utf-8";
      std::string controller = "none";
      if (state && state->control)
        controller = wininspect::controller_type_str(state->control->current_controller());
      int conns = state ? state->active_connections.load() : 0;
      auto& mc = daemon_metrics ? *daemon_metrics : core.metrics_collector_;
      resp.body = mc.to_prometheus("", conns, controller);
      return resp;
    }

    // Serve dashboard UI
    if (req.path == "/dashboard") {
      resp.content_type = "text/html; charset=utf-8";
      resp.body = DASHBOARD_HTML;
      return resp;
    }
    if (req.path == "/") {
      resp.code = 301;
      resp.status = "Moved";
      resp.body = "<html><body>Redirecting to <a href='/dashboard'>dashboard</a>...</body></html>";
      resp.content_type = "text/html";
      return resp;
    }

    // Find matching route
    const Route* matched = nullptr;
    for (auto& rt : routes) {
      if (req.method == rt.method && req.path == rt.path) {
        matched = &rt;
        break;
      }
    }

    if (!matched) {
      resp.code = 404;
      resp.status = "Not Found";
      resp.body = R"({"error":"not found"})";
      return resp;
    }

    // Enforce daemon flags
    if (no_clipboard && matched &&
        (matched->rpc_method == std::string("clipboard.read") ||
         matched->rpc_method == std::string("clipboard.write"))) {
      resp.code = 403;
      resp.status = "Forbidden";
      resp.body = R"({"error":"clipboard access disabled"})";
      return resp;
    }
    // Dispatch to core engine (read-only enforcement via MethodPolicy table)
    try {
      json::Object params;
      if (matched->param_fill)
        matched->param_fill(req, params);
      CoreRequest creq{"http-1", matched->rpc_method, params};
      auto snap = core.get_backend()->capture_snapshot();
      auto cresp = core.handle(creq, snap, nullptr);

      json::Object result;
      result["ok"] = cresp.ok;
      result["id"] = cresp.id;
      result["result"] = cresp.result;
      if (!cresp.error_code.empty())
        result["error_code"] = cresp.error_code;
      if (!cresp.error_message.empty())
        result["error_message"] = cresp.error_message;
      resp.body = json::dumps(result);
    }
    catch (const std::exception& e) {
      json::Object err;
      err["ok"] = false;
      err["error"] = std::string(e.what());
      resp.body = json::dumps(err);
    }

    return resp;
  }

  // ── HTTP Server (plain) ────────────────────────────────────────────────────

  void run_http_server(std::atomic<bool>* running, int port, CoreEngine& core,
                       MetricsCollector* daemon_metrics, ServerState* state,
                       const std::string& auth_token, bool read_only, bool no_clipboard,
                       int redirect_https_port)
  {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
      LOG_ERROR("HTTP: Winsock init failed");
      return;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
      WSACleanup();
      return;
    }

    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on));
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((u_short)port);

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
      LOG_ERROR("HTTP: bind failed on port " + std::to_string(port));
      closesocket(s);
      WSACleanup();
      return;
    }
    listen(s, SOMAXCONN);

    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);

    LOG_INFO("HTTP server listening on port " + std::to_string(port));

    while (running->load()) {
      SOCKET c = accept(s, nullptr, nullptr);
      if (c == INVALID_SOCKET) {
        Sleep(100);
        continue;
      }

      char buf[8192];
      int r = recv(c, buf, sizeof(buf) - 1, 0);
      if (r <= 0) {
        closesocket(c);
        continue;
      }
      buf[r] = '\0';

      HttpReq req;
      if (!parse_http(std::string(buf), req)) {
        HttpResp resp;
        resp.code = 400;
        resp.status = "Bad Request";
        resp.body = R"({"error":"bad request"})";
        std::string out = build_response(resp);
        send(c, out.data(), (int)out.size(), 0);
        closesocket(c);
        continue;
      }

      HttpResp resp = handle_http_request(req, core, daemon_metrics, state, auth_token, read_only,
                                          no_clipboard, redirect_https_port);

      // WebSocket upgrade?
      if (resp.code == 101) {
        send(c, resp.body.data(), (int)resp.body.size(), 0);
        int frame_id = 0;
        while (running->load()) {
          WsOpcode opcode = WsOpcode::Text;
          auto client_msg = ws_recv_frame(c, opcode);
          if (opcode == WsOpcode::Close)
            break;

          json::Object snap;
          auto bk = core.get_backend();
          if (bk) {
            snap["t"] = (double)++frame_id;
            json::Object evParams;
            evParams["wait_ms"] = 0.0;
            CoreRequest evReq{"ws", "events.poll", evParams};
            auto snap_data = bk->capture_snapshot();
            auto evResp = core.handle(evReq, snap_data, nullptr);
            if (evResp.ok)
              snap["e"] = evResp.result;
          }
          std::string payload = json::dumps(snap);
          auto compressed =
              wininspect::compress(std::vector<uint8_t>(payload.begin(), payload.end()));
          if (!compressed.empty() && compressed.size() < payload.size()) {
            std::string comp_str(compressed.begin(), compressed.end());
            if (!ws_send_frame(c, WsOpcode::Binary, comp_str))
              break;
          }
          else {
            if (!ws_send_frame(c, WsOpcode::Text, payload))
              break;
          }
          Sleep(500);
        }
        closesocket(c);
        continue;
      }

      std::string out = build_response(resp);
      send(c, out.data(), (int)out.size(), 0);
      closesocket(c);
    }

    closesocket(s);
    WSACleanup();
  }

  // ── HTTPS Server (TLS 1.3 wrapper around HTTP) ────────────────────────────

  void run_https_server(std::atomic<bool>* running, int port, wininspect::CoreEngine& core,
                        const std::string& auth_token, bool read_only, bool no_clipboard,
                        const std::string& cert_pem, const std::string& key_pem)
  {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
      LOG_ERROR("HTTPS: Winsock init failed");
      return;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
      WSACleanup();
      return;
    }

    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on));
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((u_short)port);

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
      LOG_ERROR("HTTPS: bind failed on port " + std::to_string(port));
      closesocket(s);
      WSACleanup();
      return;
    }
    listen(s, SOMAXCONN);

    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);

    LOG_INFO("HTTPS server listening on port " + std::to_string(port) + " (TLS 1.3)");

    while (running->load()) {
      SOCKET c = accept(s, nullptr, nullptr);
      if (c == INVALID_SOCKET) {
        Sleep(100);
        continue;
      }

      // TLS handshake
      TlsSession tls;
      if (!tls.init_server(cert_pem, key_pem)) {
        LOG_ERROR("HTTPS: TLS init_server failed");
        closesocket(c);
        continue;
      }
      if (!tls.handshake((uintptr_t)c)) {
        closesocket(c);
        continue;
      }

      // Read HTTP request over TLS
      std::vector<uint8_t> tls_buf;
      if (!tls.recv((uintptr_t)c, tls_buf) || tls_buf.empty()) {
        closesocket(c);
        continue;
      }
      std::string raw(tls_buf.begin(), tls_buf.end());

      HttpReq req;
      if (!parse_http(raw, req)) {
        HttpResp resp;
        resp.code = 400;
        resp.status = "Bad Request";
        resp.body = R"({"error":"bad request"})";
        std::string out = build_response(resp);
        (void)tls.send((uintptr_t)c, std::vector<uint8_t>(out.begin(), out.end()));
        closesocket(c);
        continue;
      }

      HttpResp resp =
          handle_http_request(req, core, nullptr, nullptr, auth_token, read_only, no_clipboard);

      // WebSocket over TLS (wss://)
      if (resp.code == 101) {
        // Send HTTP 101 Switching Protocols upgrade response over TLS
        std::string upgrade = resp.body;
        (void)tls.send((uintptr_t)c, std::vector<uint8_t>(upgrade.begin(), upgrade.end()));

        // WebSocket event loop with TLS-aware frame I/O
        int frame_id = 0;
        while (running->load()) {
          WsOpcode opcode = WsOpcode::Text;
          auto client_msg = ws_recv_frame(c, opcode, &tls);
          if (opcode == WsOpcode::Close)
            break;

          json::Object snap;
          auto bk = core.get_backend();
          if (bk) {
            snap["t"] = (double)++frame_id;
            json::Object evParams;
            evParams["wait_ms"] = 0.0;
            CoreRequest evReq{"wss", "events.poll", evParams};
            auto snap_data = bk->capture_snapshot();
            auto evResp = core.handle(evReq, snap_data, nullptr);
            if (evResp.ok)
              snap["e"] = evResp.result;
          }
          std::string payload = json::dumps(snap);
          auto compressed =
              wininspect::compress(std::vector<uint8_t>(payload.begin(), payload.end()));
          if (!compressed.empty() && compressed.size() < payload.size()) {
            std::string comp_str(compressed.begin(), compressed.end());
            if (!ws_send_frame(c, WsOpcode::Binary, comp_str, &tls))
              break;
          }
          else {
            if (!ws_send_frame(c, WsOpcode::Text, payload, &tls))
              break;
          }
        }
        closesocket(c);
        continue;
      }

      std::string out = build_response(resp);
      (void)tls.send((uintptr_t)c, std::vector<uint8_t>(out.begin(), out.end()));
      closesocket(c);
    }

    closesocket(s);
    WSACleanup();
  }

} // namespace wininspectd
