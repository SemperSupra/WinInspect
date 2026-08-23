// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include "doctest/doctest.h"
#include "wininspect/network_config.hpp"
#include "rendezvous_client.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

#include <thread>
#include <chrono>
#include <cstring>

using namespace wininspect;
using namespace wininspectd;

// ── Stub HTTP Server ───────────────────────────────────────────────────────

/// A minimal stub HTTP server that listens on a random port and returns
/// a fixed JSON response for any request. Runs until the shared_ptr flag
/// is set to false or the destructor runs.
struct StubRvServer
{
  std::shared_ptr<std::atomic<bool>> running;
  SOCKET listen_sock = INVALID_SOCKET;
  int port = 0;
  std::string response_body;
  int response_code = 200;

  StubRvServer(const std::string& body, int code = 200)
      : running(std::make_shared<std::atomic<bool>>(true)), response_body(body), response_code(code)
  {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCKET)
      return;

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = 0; // OS assigns port
    if (bind(listen_sock, (sockaddr*)&addr, sizeof(addr)) != 0)
      return;
    listen(listen_sock, 1);

    // Get assigned port
    sockaddr_in bound = {};
    int bound_len = sizeof(bound);
    getsockname(listen_sock, (sockaddr*)&bound, &bound_len);
    port = ntohs(bound.sin_port);

    // Spawn listener thread
    auto r = running;
    auto s = listen_sock;
    auto resp_body = response_body;
    auto resp_code_val = response_code;
    std::thread([r, s, resp_body, resp_code_val]() {
      ::Sleep(50);
      while (r->load()) {
        SOCKET c = accept(s, nullptr, nullptr);
        if (c == INVALID_SOCKET)
          break;

        char buf[4096];
        int n = recv(c, buf, sizeof(buf) - 1, 0);
        if (n > 0)
          buf[n] = '\0';

        // Build HTTP response
        std::string http_resp = "HTTP/1.1 " + std::to_string(resp_code_val) +
                                " OK\r\n"
                                "Content-Type: application/json\r\n"
                                "Content-Length: " +
                                std::to_string(resp_body.size()) +
                                "\r\n"
                                "Connection: close\r\n\r\n" +
                                resp_body;
        send(c, http_resp.data(), (int)http_resp.size(), 0);
        closesocket(c);
      }
      closesocket(s);
    }).detach();
  }

  ~StubRvServer()
  {
    *running = false;
    if (listen_sock != INVALID_SOCKET)
      closesocket(listen_sock);
  }

  std::string url() const
  {
    return "http://127.0.0.1:" + std::to_string(port) + "/api/v1/rendezvous";
  }
};

// ── Tests ──────────────────────────────────────────────────────────────────

DOCTEST_TEST_CASE("rendezvous: register succeeds")
{
  StubRvServer server(R"({"ok":true})");

  bool ok =
      rendezvous_register(server.url(), "test-key", "uuid-123", "test-daemon", "127.0.0.1", 1985);
  CHECK(ok);
}

DOCTEST_TEST_CASE("rendezvous: heartbeat succeeds")
{
  StubRvServer server(R"({"ok":true})");

  bool ok = rendezvous_heartbeat(server.url(), "test-key", "uuid-123");
  CHECK(ok);
}

DOCTEST_TEST_CASE("rendezvous: deregister succeeds")
{
  StubRvServer server(R"({"ok":true})");

  bool ok = rendezvous_deregister(server.url(), "test-key", "uuid-123");
  CHECK(ok);
}

DOCTEST_TEST_CASE("rendezvous: server error returns false")
{
  StubRvServer server(R"({"error":"bad request"})", 400);

  bool ok = rendezvous_register(server.url(), "test-key", "uuid-123", "test", "127.0.0.1", 1985);
  CHECK(!ok);
}

DOCTEST_TEST_CASE("rendezvous: connection refused returns false")
{
  // No server running — connection should fail
  bool ok = rendezvous_register("http://127.0.0.1:1/api/v1/rendezvous", "key", "uuid", "test",
                                "::1", 1985);
  CHECK(!ok);
}

DOCTEST_TEST_CASE("rendezvous: server timeout returns false")
{
  // Port with no listener (unlikely to be in use, but not guaranteed)
  // We just verify the function handles it gracefully
  bool ok = rendezvous_heartbeat("http://127.0.0.1:65535/api/v1/rendezvous", "key", "uuid");
  CHECK(!ok);
}

DOCTEST_TEST_CASE("rendezvous: empty server response returns true if 200")
{
  StubRvServer server("", 200); // empty body, 200 OK

  bool ok = rendezvous_deregister(server.url(), "key", "uuid");
  CHECK(ok); // 200 status code is success regardless of body
}

DOCTEST_TEST_CASE("rendezvous: invalid URL returns false")
{
  bool ok = rendezvous_register("not-a-valid-url", "key", "uuid", "name", "127.0.0.1", 80);
  CHECK(!ok);
}

DOCTEST_TEST_CASE("rendezvous: full lifecycle")
{
  StubRvServer server(R"({"ok":true})");

  // Register
  CHECK(rendezvous_register(server.url(), "key", "uuid-1", "box", "10.0.0.1", 1985));

  // Heartbeat
  CHECK(rendezvous_heartbeat(server.url(), "key", "uuid-1"));

  // Deregister
  CHECK(rendezvous_deregister(server.url(), "key", "uuid-1"));
}
