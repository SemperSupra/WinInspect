// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

// Rendezvous server — a lightweight HTTP service for daemon discovery.
//
// API:
//   POST /api/v1/rendezvous/register  — register a daemon instance
//   PUT  /api/v1/rendezvous/heartbeat/<uuid> — keep registration alive
//   DELETE /api/v1/rendezvous/instances/<uuid> — deregister
//   GET  /api/v1/rendezvous/instances — list all registered instances

#include "wininspect/tinyjson.hpp"
#include "wininspect/logger.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>
#include <sstream>
#include <iostream>
#include <functional>

using namespace wininspect;

namespace {

struct RegisteredInstance {
  std::string uuid;
  std::string name;
  std::string host;
  int port{};
  std::string pubkey;
  json::Object extra;
  std::chrono::steady_clock::time_point last_heartbeat;
  int heartbeat_ttl_sec = 60;
};

std::map<std::string, RegisteredInstance> g_instances;
std::mutex g_instances_mu;

struct HttpRequest {
  std::string method;
  std::string path;
  std::string body;
  std::map<std::string, std::string> headers;
};

struct HttpResponse {
  int status_code = 200;
  std::string status_text = "OK";
  std::string content_type = "application/json";
  std::string body;
};

static std::string build_http_response(const HttpResponse &resp) {
  std::ostringstream ss;
  ss << "HTTP/1.1 " << resp.status_code << " " << resp.status_text << "\r\n"
     << "Content-Type: " << resp.content_type << "\r\n"
     << "Content-Length: " << resp.body.size() << "\r\n"
     << "Connection: close\r\n"
     << "Access-Control-Allow-Origin: *\r\n"
     << "\r\n"
     << resp.body;
  return ss.str();
}

static bool parse_http_request(const std::string &raw, HttpRequest &req) {
  auto line_end = raw.find("\r\n");
  if (line_end == std::string::npos) return false;
  auto first_line = raw.substr(0, line_end);
  auto sp1 = first_line.find(' ');
  auto sp2 = first_line.rfind(' ');
  if (sp1 == std::string::npos || sp2 == std::string::npos || sp1 == sp2) return false;
  req.method = first_line.substr(0, sp1);
  req.path = first_line.substr(sp1 + 1, sp2 - sp1 - 1);
  size_t pos = line_end + 2;
  while (pos < raw.size()) {
    auto hdr_end = raw.find("\r\n", pos);
    if (hdr_end == std::string::npos) break;
    if (hdr_end == pos) { pos = hdr_end + 2; break; }
    auto colon = raw.find(':', pos);
    if (colon != std::string::npos && colon < hdr_end) {
      std::string key = raw.substr(pos, colon - pos);
      std::string val = raw.substr(colon + 2, hdr_end - colon - 2);
      req.headers[key] = val;
    }
    pos = hdr_end + 2;
  }
  if (pos < raw.size()) req.body = raw.substr(pos);
  return true;
}

static void handle_register(const HttpRequest &req, HttpResponse &resp) {
  try {
    auto v = json::parse(req.body);
    if (!v.is_obj()) { resp.status_code = 400; resp.body = R"({"error":"expected object"})"; return; }
    auto obj = v.as_obj();
    auto it_uuid = obj.find("uuid");
    if (it_uuid == obj.end() || !it_uuid->second.is_str() || it_uuid->second.as_str().empty()) {
      resp.status_code = 400; resp.body = R"({"error":"missing uuid"})"; return;
    }
    RegisteredInstance inst;
    inst.uuid = it_uuid->second.as_str();
    auto it = obj.find("name"); if (it != obj.end() && it->second.is_str()) inst.name = it->second.as_str();
    auto it2 = obj.find("host"); if (it2 != obj.end() && it2->second.is_str()) inst.host = it2->second.as_str();
    auto it3 = obj.find("port"); if (it3 != obj.end() && it3->second.is_num()) inst.port = (int)it3->second.as_num();
    auto it4 = obj.find("pubkey"); if (it4 != obj.end() && it4->second.is_str()) inst.pubkey = it4->second.as_str();
    inst.last_heartbeat = std::chrono::steady_clock::now();
    {
      std::lock_guard<std::mutex> lk(g_instances_mu);
      g_instances[inst.uuid] = std::move(inst);
    }
    json::Object resp_o;
    resp_o["ok"] = true; resp_o["uuid"] = it_uuid->second.as_str();
    resp.body = json::dumps(resp_o);
    resp.status_code = 201;
  } catch (const std::exception &e) {
    resp.status_code = 400;
    json::Object err; err["error"] = std::string(e.what());
    resp.body = json::dumps(err);
  }
}

static void handle_heartbeat(const std::string &uuid, HttpResponse &resp) {
  std::lock_guard<std::mutex> lk(g_instances_mu);
  auto it = g_instances.find(uuid);
  if (it == g_instances.end()) {
    resp.status_code = 404; resp.body = R"({"error":"instance not found"})"; return;
  }
  it->second.last_heartbeat = std::chrono::steady_clock::now();
  json::Object o; o["ok"] = true; resp.body = json::dumps(o);
}

static void handle_deregister(const std::string &uuid, HttpResponse &resp) {
  std::lock_guard<std::mutex> lk(g_instances_mu);
  g_instances.erase(uuid);
  json::Object o; o["ok"] = true; resp.body = json::dumps(o);
}

static void handle_list_instances(HttpResponse &resp) {
  std::lock_guard<std::mutex> lk(g_instances_mu);
  auto now = std::chrono::steady_clock::now();
  for (auto it = g_instances.begin(); it != g_instances.end(); ) {
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.last_heartbeat).count();
    if (elapsed > it->second.heartbeat_ttl_sec * 3)
      it = g_instances.erase(it);
    else ++it;
  }
  json::Array arr;
  for (auto &[uuid, inst] : g_instances) {
    json::Object o;
    o["uuid"] = inst.uuid; o["name"] = inst.name; o["host"] = inst.host; o["port"] = (double)inst.port;
    if (!inst.pubkey.empty()) o["pubkey"] = inst.pubkey;
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - inst.last_heartbeat).count();
    o["last_seen"] = (double)elapsed;
    arr.push_back(o);
  }
  resp.body = json::dumps(arr);
}

static void handle_request(const HttpRequest &req, HttpResponse &resp) {
  if (req.method == "OPTIONS") { resp.status_code = 204; return; }
  if (req.method == "POST" && req.path == "/api/v1/rendezvous/register")
    return handle_register(req, resp);
  if (req.method == "PUT") {
    std::string p = "/api/v1/rendezvous/heartbeat/";
    if (req.path.compare(0, p.size(), p) == 0) return handle_heartbeat(req.path.substr(p.size()), resp);
  }
  if (req.method == "DELETE") {
    std::string p = "/api/v1/rendezvous/instances/";
    if (req.path.compare(0, p.size(), p) == 0) return handle_deregister(req.path.substr(p.size()), resp);
  }
  if (req.method == "GET" && req.path == "/api/v1/rendezvous/instances")
    return handle_list_instances(resp);
  if (req.method == "GET" && req.path == "/health") {
    json::Object o; o["ok"] = true; o["instances"] = (double)g_instances.size();
    resp.body = json::dumps(o); return;
  }
  resp.status_code = 404; resp.status_text = "Not Found";
  json::Object err; err["error"] = "not found"; resp.body = json::dumps(err);
}

} // anonymous namespace

int main(int argc, char **argv) {
  int port = 8080;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--port" && i + 1 < argc) port = std::stoi(argv[++i]);
    if (std::string(argv[i]) == "--help") {
      std::cerr << "WinInspect Rendezvous Server\nUsage: wininspect-rendezvous [--port <n>]\n"; return 0;
    }
  }

  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { LOG_ERROR("Winsock init failed"); return 1; }

  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) { LOG_ERROR("socket() failed"); WSACleanup(); return 1; }

  int on = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons((u_short)port);

  if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
    LOG_ERROR("bind() failed on port " + std::to_string(port)); closesocket(s); WSACleanup(); return 1;
  }
  if (listen(s, SOMAXCONN) == SOCKET_ERROR) { LOG_ERROR("listen() failed"); closesocket(s); WSACleanup(); return 1; }

  LOG_INFO("Rendezvous server listening on port " + std::to_string(port));

  u_long mode = 1;
  ioctlsocket(s, FIONBIO, &mode);
  std::atomic<bool> running{true};

  while (running.load()) {
    SOCKET client = accept(s, nullptr, nullptr);
    if (client == INVALID_SOCKET) { if (WSAGetLastError() == WSAEWOULDBLOCK) { Sleep(100); continue; } break; }

    char buf[8192];
    int r = recv(client, buf, sizeof(buf) - 1, 0);
    if (r > 0) {
      buf[r] = '\0';
      HttpRequest req;
      if (parse_http_request(std::string(buf), req)) {
        HttpResponse resp;
        handle_request(req, resp);
        std::string response = build_http_response(resp);
        send(client, response.data(), (int)response.size(), 0);
        LOG_DEBUG("Rendezvous: " + req.method + " " + req.path + " -> " + std::to_string(resp.status_code));
      }
    }
    closesocket(client);
  }

  closesocket(s); WSACleanup();
  return 0;
}
