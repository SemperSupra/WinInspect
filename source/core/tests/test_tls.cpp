// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include "doctest/doctest.h"
#include "wininspect/tls.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

#include <string>
#include <thread>
#include <chrono>

using namespace wininspect;

// ── Helpers ─────────────────────────────────────────────────────────────────

static bool init_winsock()
{
  WSADATA wsa;
  return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
}

static void cleanup_winsock()
{
  WSACleanup();
}

#ifdef WININSPECT_HAVE_OPENSSL

// ── Self-signed certificate generation ──────────────────────────────────────

TEST_CASE("TlsSession::generate_self_signed_cert")
{
  std::string cert, key;
  bool ok = TlsSession::generate_self_signed_cert("test.example.com", cert, key);
  REQUIRE(ok);
  // Cert should contain PEM markers
  CHECK(cert.find("-----BEGIN CERTIFICATE-----") != std::string::npos);
  CHECK(cert.find("-----END CERTIFICATE-----") != std::string::npos);
  // Key should contain PEM markers
  CHECK(key.find("-----BEGIN ") != std::string::npos);
  CHECK((key.find("PRIVATE KEY-----") != std::string::npos ||
         key.find("EC PRIVATE KEY-----") != std::string::npos));
  CHECK(key.find("-----END") != std::string::npos);
}

// ── Server init with generated cert ─────────────────────────────────────────

TEST_CASE("TlsSession::init_server with generated cert")
{
  std::string cert, key;
  REQUIRE(TlsSession::generate_self_signed_cert("test.local", cert, key));

  TlsSession session;
  CHECK(session.init_server(cert, key));
  CHECK(!session.is_initialized()); // Not initialized until handshake
}

// ── Server init with invalid cert ───────────────────────────────────────────

TEST_CASE("TlsSession::init_server rejects invalid cert")
{
  TlsSession session;
  CHECK(!session.init_server("not-a-cert", "not-a-key"));
}

// ── Server init with mismatched cert/key ────────────────────────────────────

TEST_CASE("TlsSession::init_server rejects mismatched key")
{
  std::string cert1, key1, cert2, key2;
  REQUIRE(TlsSession::generate_self_signed_cert("one.local", cert1, key1));
  REQUIRE(TlsSession::generate_self_signed_cert("two.local", cert2, key2));

  TlsSession session;
  // Cert from pair 1, key from pair 2 — should fail
  CHECK(!session.init_server(cert1, key2));
}

// ── Client init ─────────────────────────────────────────────────────────────

TEST_CASE("TlsSession::init_client")
{
  TlsSession session;
  CHECK(session.init_client());
  CHECK(!session.is_initialized()); // Not initialized until handshake
}

// ── Handshake fails on invalid socket ───────────────────────────────────────

TEST_CASE("TlsSession::handshake fails on invalid socket")
{
  std::string cert, key;
  REQUIRE(TlsSession::generate_self_signed_cert("test.local", cert, key));

  TlsSession session;
  REQUIRE(session.init_server(cert, key));
  // Pass an invalid socket handle — should fail gracefully
  CHECK(!session.handshake((uintptr_t)INVALID_SOCKET));
}

// ── Encrypt/decrypt round trip via loopback socket ──────────────────────────

TEST_CASE("TlsSession TLS 1.3 loopback handshake and encrypt/decrypt")
{
  REQUIRE(init_winsock());

  // Create a socket pair for loopback communication
  SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  REQUIRE(listener != INVALID_SOCKET);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  addr.sin_port = 0; // OS picks a free port
  REQUIRE(bind(listener, (struct sockaddr*)&addr, sizeof(addr)) == 0);

  int addrlen = sizeof(addr);
  getsockname(listener, (struct sockaddr*)&addr, &addrlen);
  REQUIRE(listen(listener, 1) == 0);

  // Generate cert
  std::string cert, key;
  REQUIRE(TlsSession::generate_self_signed_cert("loopback.test", cert, key));

  bool server_ok = false;
  bool client_ok = false;
  std::string received;

  std::thread server_thread([&]() {
    SOCKET c = accept(listener, nullptr, nullptr);
    if (c == INVALID_SOCKET)
      return;

    TlsSession tls;
    if (!tls.init_server(cert, key)) {
      closesocket(c);
      return;
    }
    if (!tls.handshake((uintptr_t)c)) {
      closesocket(c);
      return;
    }

    // Read message from client
    std::vector<uint8_t> data;
    if (tls.recv((uintptr_t)c, data)) {
      received.assign(data.begin(), data.end());
      server_ok = true;

      // Echo back
      (void)tls.send((uintptr_t)c, std::vector<uint8_t>(received.begin(), received.end()));
    }
    closesocket(c);
  });

  // Give server thread time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::thread client_thread([&]() {
    SOCKET c = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (c == INVALID_SOCKET)
      return;
    if (connect(c, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
      closesocket(c);
      return;
    }

    TlsSession tls;
    if (!tls.init_client()) {
      closesocket(c);
      return;
    }
    if (!tls.handshake((uintptr_t)c)) {
      closesocket(c);
      return;
    }

    // Send a message
    std::string msg = "Hello over TLS 1.3!";
    std::vector<uint8_t> payload(msg.begin(), msg.end());
    (void)tls.send((uintptr_t)c, payload);

    // Read echo
    std::vector<uint8_t> echo;
    if (tls.recv((uintptr_t)c, echo)) {
      std::string echoed(echo.begin(), echo.end());
      if (echoed == msg)
        client_ok = true;
    }
    closesocket(c);
  });

  server_thread.join();
  client_thread.join();
  closesocket(listener);
  cleanup_winsock();

  CHECK(server_ok);
  CHECK(client_ok);
  CHECK(received == "Hello over TLS 1.3!");
}

// ── Multiple independent sessions ───────────────────────────────────────────

TEST_CASE("TlsSession multiple independent instances")
{
  std::string cert1, key1, cert2, key2;
  REQUIRE(TlsSession::generate_self_signed_cert("a.local", cert1, key1));
  REQUIRE(TlsSession::generate_self_signed_cert("b.local", cert2, key2));

  TlsSession s1, s2;
  CHECK(s1.init_server(cert1, key1));
  CHECK(s2.init_server(cert2, key2));
  // Both sessions should be independent and not initialized
  CHECK(!s1.is_initialized());
  CHECK(!s2.is_initialized());
}

#else // !WININSPECT_HAVE_OPENSSL

// ── Non-OpenSSL stub returns false ──────────────────────────────────────────

TEST_CASE("TlsSession stub without OpenSSL returns false gracefully")
{
  TlsSession session;
  CHECK(!session.is_initialized());
  std::string cert, key;
  CHECK(!TlsSession::generate_self_signed_cert("test.local", cert, key));
  CHECK(!session.init_server("cert", "key"));
  CHECK(!session.init_client());
  CHECK(!session.handshake(0));
}

#endif // WININSPECT_HAVE_OPENSSL
