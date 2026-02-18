#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "wininspect/core.hpp"
#include "wininspect/tinyjson.hpp"

#include <filesystem>
#include <fstream>

#include "wininspect/crypto.hpp"

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Advapi32.lib") // For CryptGenRandom

static const wchar_t *PIPE_NAME = L"\\\\.\\pipe\\wininspectd";

struct Conn {
    bool is_tcp = false;
    SOCKET s = INVALID_SOCKET;
    HANDLE hPipe = INVALID_HANDLE_VALUE;

    bool send(const std::string& data) {
        uint32_t len = (uint32_t)data.size();
        if (is_tcp) {
            if (::send(s, (const char*)&len, 4, 0) != 4) return false;
            return ::send(s, data.data(), (int)len, 0) == (int)len;
        } else {
            DWORD w = 0;
            if (!WriteFile(hPipe, &len, 4, &w, nullptr)) return false;
            return WriteFile(hPipe, data.data(), len, &w, nullptr) != FALSE;
        }
    }

    bool recv(std::string& out) {
        uint32_t len = 0;
        if (is_tcp) {
            if (::recv(s, (char*)&len, 4, 0) != 4) return false;
            out.resize(len);
            uint32_t n = 0;
            while (n < len) {
                int r = ::recv(s, out.data() + n, (int)(len - n), 0);
                if (r <= 0) return false;
                n += r;
            }
            return true;
        } else {
            DWORD r = 0;
            if (!ReadFile(hPipe, &len, 4, &r, nullptr)) return false;
            out.resize(len);
            if (!ReadFile(hPipe, out.data(), len, &r, nullptr)) return false;
            return true;
        }
    }

    void close() {
        if (is_tcp && s != INVALID_SOCKET) { closesocket(s); s = INVALID_SOCKET; }
        if (!is_tcp && hPipe != INVALID_HANDLE_VALUE) { CloseHandle(hPipe); hPipe = INVALID_HANDLE_VALUE; }
    }

    ~Conn() { close(); }
};

static std::string get_config_path() {
  const char *home = getenv("USERPROFILE");
  if (!home)
    home = getenv("HOME");
  if (!home)
    return ".wininspect_config";
  return std::string(home) + "/.wininspect_config";
}

static void save_key_path(const std::string &path) {
  std::ofstream f(get_config_path());
  f << path;
}

static std::string load_key_path() {
  std::ifstream f(get_config_path());
  std::string s;
  std::getline(f, s);
  return s;
}

static std::vector<uint8_t> base64_decode(const std::string &in) {
  static const std::string b64 =
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

static bool perform_auth(Conn &conn) {
  std::string challenge_json;
  if (!conn.recv(challenge_json))
    return false;
  auto v = wininspect::json::parse(challenge_json);
  if (!v.is_obj() || v.as_obj().at("type").as_str() != "hello")
    return true; // No auth required (or old daemon)

  std::string nonce_b64 = v.as_obj().at("nonce").as_str();
  std::string key_path = load_key_path();
  if (key_path.empty()) {
    std::cerr << "Daemon requires authentication. Set key with: wininspect "
                 "config --key <path>\n";
    return false;
  }

  std::string sig =
      wininspect::crypto::sign_ssh_msg(base64_decode(nonce_b64), key_path);
  if (sig.empty()) {
    std::cerr << "Failed to sign challenge with key: " << key_path << "\n";
    return false;
  }

  wininspect::json::Object resp;
  resp["version"] = std::string(wininspect::PROTOCOL_VERSION);
  resp["identity"] = "wininspect-user";
  resp["signature"] = sig;
  if (!conn.send(wininspect::json::dumps(resp)))
    return false;

  std::string status_json;
  if (!conn.recv(status_json))
    return false;
  auto sv = wininspect::json::parse(status_json);
  return sv.is_obj() && sv.as_obj().at("type").as_str() == "auth_status" &&
         sv.as_obj().at("ok").as_bool();
}

static bool connect_daemon(Conn &conn, bool tcp, const std::string &host,
                           int port) {
  if (tcp) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
      return false;
    conn.s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (conn.s == INVALID_SOCKET)
      return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    if (connect(conn.s, (sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
      closesocket(conn.s);
      return false;
    }
    conn.is_tcp = true;
    if (!perform_auth(conn)) {
      conn.close();
      return false;
    }
    return true;
  } else {
    conn.hPipe = CreateFileW(PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0,
                             nullptr, OPEN_EXISTING, 0, nullptr);
    if (conn.hPipe == INVALID_HANDLE_VALUE)
      return false;
    conn.is_tcp = false;
    return true;
  }
}

static std::string make_req(const std::string &id, const std::string &method,
                            wininspect::json::Object params) {
  using namespace wininspect::json;
  Object o;
  o["id"] = id;
  o["method"] = method;
  o["params"] = params;
  return dumps(o);
}

static int usage() {
  std::cerr << "Usage: wininspect <command> [args] [--tcp host:port]\n"
            << "Commands:\n"
            << "  capture\n"
            << "  top [--snapshot s-..]\n"
            << "  info <hwnd> [--snapshot s-..]\n"
            << "  children <hwnd> [--snapshot s-..]\n"
            << "  pick <x> <y> [--snapshot s-..]\n"
            << "  events-poll <new_snap_id> [old_snap_id]\n"
            << "  events-subscribe\n"
            << "  events-unsubscribe\n"
            << "  watch\n"
            << "  status\n"
            << "  ensure-visible <hwnd> <true|false>\n"
            << "  ensure-foreground <hwnd>\n"
            << "  post-message <hwnd> <msg> [wparam] [lparam]\n"
            << "  send-input <base64_data>\n"
            << "  config --key <path>\n";
  return 2;
}

int main(int argc, char **argv) {
  if (argc < 2)
    return usage();

  bool use_tcp = false;
  std::string tcp_host = "127.0.0.1";
  int tcp_port = 1985;

  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--tcp") {
      use_tcp = true;
      if (i + 1 < argc) {
        std::string host_port = argv[i + 1];
        size_t colon = host_port.find(':');
        if (colon != std::string::npos) {
          tcp_host = host_port.substr(0, colon);
          tcp_port = std::stoi(host_port.substr(colon + 1));
        } else {
          tcp_host = host_port;
        }
        i++;
      }
    } else {
      args.push_back(argv[i]);
    }
  }

  if (args.empty())
    return usage();
  std::string cmd = args[0];

  using namespace wininspect::json;
  Object params;
  params["canonical"] = true;

  auto get_snapshot = [&](size_t &i) {
    if (i + 1 < args.size() && args[i] == "--snapshot") {
      params["snapshot_id"] = args[i + 1];
      i += 2;
      return true;
    }
    return false;
  };

  auto send_and_print = [&](const std::string &method) {
    Conn conn;
    if (!connect_daemon(conn, use_tcp, tcp_host, tcp_port)) {
      std::cerr << "failed to connect to daemon\n";
      return 1;
    }
    std::string req = make_req("cli-1", method, params);
    std::string resp;
    if (!conn.send(req) || !conn.recv(resp)) {
      std::cerr << "communication error\n";
      conn.close();
      return 1;
    }
    std::cout << resp << "\n";
    conn.close();
    return 0;
  };

  if (cmd == "capture") {
    return send_and_print("snapshot.capture");
  }

  if (cmd == "top") {
    for (size_t i = 1; i < args.size();)
      if (!get_snapshot(i))
        i++;
    return send_and_print("window.listTop");
  }

  if (cmd == "info") {
    if (args.size() < 2)
      return usage();
    params["hwnd"] = args[1];
    for (size_t i = 2; i < args.size();)
      if (!get_snapshot(i))
        i++;
    return send_and_print("window.getInfo");
  }

  if (cmd == "children") {
    if (args.size() < 2)
      return usage();
    params["hwnd"] = args[1];
    for (size_t i = 2; i < args.size();)
      if (!get_snapshot(i))
        i++;
    return send_and_print("window.listChildren");
  }

  if (cmd == "pick") {
    if (args.size() < 3)
      return usage();
    params["x"] = std::stod(args[1]);
    params["y"] = std::stod(args[2]);
    for (size_t i = 3; i < args.size();)
      if (!get_snapshot(i))
        i++;
    return send_and_print("window.pickAtPoint");
  }

  if (cmd == "events-poll") {
    if (args.size() < 2)
      return usage();
    params["snapshot_id"] = args[1];
    if (args.size() > 2)
      params["old_snapshot_id"] = args[2];
    return send_and_print("events.poll");
  }

  if (cmd == "events-subscribe") {
    return send_and_print("events.subscribe");
  }

  if (cmd == "events-unsubscribe") {
    return send_and_print("events.unsubscribe");
  }

  if (cmd == "watch") {
    Conn conn;
    if (!connect_daemon(conn, use_tcp, tcp_host, tcp_port))
      return 1;

    std::string resp;
    std::cout << "Watching for window events... (Ctrl+C to stop)\n";

    // Initialize baseline snapshot
    conn.send(make_req("w-0", "events.poll", params));
    conn.recv(resp);

    while (true) {
      Sleep(1000);
      conn.send(make_req("w-1", "events.poll", params));
      if (conn.recv(resp)) {
        std::cout << resp << "\n";
      }
    }
    return 0;
  }

  if (cmd == "status") {
    return send_and_print("daemon.status");
  }

  if (cmd == "ensure-visible") {
    if (args.size() < 3)
      return usage();
    params["hwnd"] = args[1];
    params["visible"] = (args[2] == "true");
    return send_and_print("window.ensureVisible");
  }

  if (cmd == "ensure-foreground") {
    if (args.size() < 2)
      return usage();
    params["hwnd"] = args[1];
    return send_and_print("window.ensureForeground");
  }

  if (cmd == "post-message") {
    if (args.size() < 3)
      return usage();
    params["hwnd"] = args[1];
    params["msg"] = std::stod(args[2]);
    if (args.size() > 3)
      params["wparam"] = std::stod(args[3]);
    if (args.size() > 4)
      params["lparam"] = std::stod(args[4]);
    return send_and_print("window.postMessage");
  }

  if (cmd == "send-input") {
    if (args.size() < 2)
      return usage();
    params["data_b64"] = args[1];
    return send_and_print("input.send");
  }

  if (cmd == "config") {
    if (args.size() >= 3 && args[1] == "--key") {
      save_key_path(args[2]);
      std::cout << "Key path saved: " << args[2] << "\n";
      return 0;
    }
    return usage();
  }

  return usage();
}
#else
int main() { return 0; }
#endif
