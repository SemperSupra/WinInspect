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
  HANDLE hPipe = INVALID_HANDLE_VALUE;
  SOCKET s = INVALID_SOCKET;
  bool is_tcp = false;

  void close() {
    if (is_tcp) {
      if (s != INVALID_SOCKET) {
        closesocket(s);
        s = INVALID_SOCKET;
      }
    } else {
      if (hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(hPipe);
        hPipe = INVALID_HANDLE_VALUE;
      }
    }
  }

  bool send(const std::string &m) {
    if (is_tcp) {
      uint32_t len = (uint32_t)m.size();
      if (::send(s, (const char *)&len, 4, 0) <= 0)
        return false;
      if (::send(s, m.data(), (int)len, 0) <= 0)
        return false;
      return true;
    } else {
      DWORD written;
      uint32_t len = (uint32_t)m.size();
      if (!WriteFile(hPipe, &len, 4, &written, nullptr))
        return false;
      if (!WriteFile(hPipe, m.data(), (DWORD)len, &written, nullptr))
        return false;
      return true;
    }
  }

  bool recv(std::string &m) {
    if (is_tcp) {
      uint32_t len;
      int r = ::recv(s, (char *)&len, 4, 0);
      if (r <= 0)
        return false;
      m.resize(len);
      r = ::recv(s, m.data(), (int)len, 0);
      if (r <= 0)
        return false;
      return true;
    } else {
      DWORD read;
      uint32_t len;
      if (!ReadFile(hPipe, &len, 4, &read, nullptr))
        return false;
      m.resize(len);
      if (!ReadFile(hPipe, m.data(), (DWORD)len, &read, nullptr))
        return false;
      return true;
    }
  }
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

    u_long mode = 1;
    ioctlsocket(conn.s, FIONBIO, &mode);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    
    connect(conn.s, (sockaddr *)&addr, sizeof(addr));

    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(conn.s, &write_fds);
    timeval tv{2, 0};

    if (select(0, NULL, &write_fds, NULL, &tv) <= 0) {
      closesocket(conn.s);
      return false;
    }

    mode = 0;
    ioctlsocket(conn.s, FIONBIO, &mode);

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
            << "  discover\n"
            << "  capture\n"
            << "  top [--snapshot s-..]\n"
            << "  info <hwnd> [--snapshot s-..]\n"
            << "  children <hwnd> [--snapshot s-..]\n"
            << "  tree [hwnd] [--snapshot s-..]\n"
            << "  pick <x> <y> [--snapshot s-..]\n"
            << "  highlight <hwnd>\n"
            << "  set-prop <hwnd> <name> <value>\n"
            << "  control-click <hwnd> <x> <y> [button]\n"
            << "  control-send <hwnd> <text>\n"
            << "  get-pixel <x> <y>\n"
            << "  pixel-search <left> <top> <right> <bottom> <r> <g> <b> [variation]\n"
            << "  capture <left> <top> <right> <bottom>\n"
            << "  ps\n"
            << "  kill <pid>\n"
            << "  file-info <path>\n"
            << "  file-read <path>\n"
            << "  find-regex [title_regex] [class_regex]\n"
            << "  reg-read <path>\n"
            << "  reg-write <path> <name> <type> <data>\n"
            << "  reg-delete <path> [name]\n"
            << "  clip-read\n"
            << "  clip-write <text>\n"
            << "  svc-list\n"
            << "  svc-status <name>\n"
            << "  svc-control <name> <start|stop>\n"
            << "  env-get\n"
            << "  env-set <name> <value>\n"
            << "  wine-drives\n"
            << "  wine-overrides\n"
            << "  mutex-check <name>\n"
            << "  mutex-create <name> [own]\n"
            << "  mem-read <pid> <address> <size>\n"
            << "  mem-write <pid> <address> <base64_data>\n"
            << "  image-match <left> <top> <right> <bottom> <base64_bmp>\n"
            << "  input-hook <true|false>\n"
            << "  events-poll <new_snap_id> [old_snap_id] [--wait-ms ms]\n"
            << "  events-subscribe\n"
            << "  events-unsubscribe\n"
            << "  watch\n"
            << "  status\n"
            << "  ensure-visible <hwnd> <true|false>\n"
            << "  ensure-foreground <hwnd>\n"
            << "  post-message <hwnd> <msg> [wparam] [lparam]\n"
            << "  send-input <base64_data>\n"
            << "  ui-inspect <hwnd>\n"
            << "  ui-invoke <hwnd> <automation_id>\n"
            << "  health\n"
            << "  config --key <path>\n";
  return 2;
}

int main(int argc, char **argv) {
  if (argc < 2)
    return usage();

  bool use_tcp = false;
  std::string tcp_host = "127.0.0.1";
  int tcp_port = 1985;
  std::string session_id_arg;

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
    } else if (std::string(argv[i]) == "--session-id" && i + 1 < argc) {
      session_id_arg = argv[i+1];
      i++;
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
  if (!session_id_arg.empty()) {
    params["session_id"] = session_id_arg;
  }

  auto get_snapshot = [&](size_t &i) {
    if (i + 1 < args.size() && args[i] == "--snapshot") {
      params["snapshot_id"] = args[i + 1];
      i += 2;
      return true;
    }
    return false;
  };

  auto send_and_print = [&](const std::string &method) -> int {
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

  if (cmd == "discover") {
    int disc_port = 1986;
    int disc_timeout_ms = 2000;

    for (int i = 1; i < argc; ++i) {
      if (std::string(argv[i]) == "--discovery-port" && i + 1 < argc) {
        disc_port = std::stoi(argv[++i]);
      }
      if (std::string(argv[i]) == "--discovery-timeout" && i + 1 < argc) {
        disc_timeout_ms = std::stoi(argv[++i]);
      }
    }

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    bool broadcast = true;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char *)&broadcast, sizeof(broadcast));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)disc_port);
    addr.sin_addr.s_addr = INADDR_BROADCAST;

    std::string msg = "WININSPECT_DISCOVER";
    sendto(s, msg.data(), (int)msg.size(), 0, (struct sockaddr *)&addr, sizeof(addr));

    // Also try loopback directly as broadcast is often blocked/restricted in containers
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    sendto(s, msg.data(), (int)msg.size(), 0, (struct sockaddr *)&addr, sizeof(addr));

    addr.sin_addr.s_addr = INADDR_ANY;
    sendto(s, msg.data(), (int)msg.size(), 0, (struct sockaddr *)&addr, sizeof(addr));

    std::cout << "Scanning for WinInspect daemons on port " << disc_port << "...\n";
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(s, &fds);
    timeval tv{disc_timeout_ms / 1000, (disc_timeout_ms % 1000) * 1000};

    while (select(0, &fds, NULL, NULL, &tv) > 0) {
      char buf[1024];
      struct sockaddr_in from;
      int from_len = sizeof(from);
      int r = recvfrom(s, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&from, &from_len);
      if (r > 0) {
        buf[r] = '\0';
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &from.sin_addr, ip, INET_ADDRSTRLEN);
        std::cout << "[" << ip << "] " << buf << "\n";
      }
      FD_ZERO(&fds);
      FD_SET(s, &fds);
      tv = {0, 500000}; // quick check for more
    }
    closesocket(s);
    return 0;
  }

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

  if (cmd == "tree") {
    if (args.size() >= 2 && args[1].rfind("0x", 0) == 0) {
      params["hwnd"] = args[1];
      for (size_t i = 2; i < args.size();)
        if (!get_snapshot(i))
          i++;
    } else {
      for (size_t i = 1; i < args.size();)
        if (!get_snapshot(i))
          i++;
    }
    return send_and_print("window.getTree");
  }

  if (cmd == "highlight") {
    if (args.size() < 2)
      return usage();
    params["hwnd"] = args[1];
    return send_and_print("window.highlight");
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

  if (cmd == "set-prop") {
    if (args.size() < 4) return usage();
    params["hwnd"] = args[1];
    params["name"] = args[2];
    params["value"] = args[3];
    return send_and_print("window.setProperty");
  }

  if (cmd == "control-click") {
    if (args.size() < 4) return usage();
    params["hwnd"] = args[1];
    params["x"] = std::stod(args[2]);
    params["y"] = std::stod(args[3]);
    if (args.size() > 4) params["button"] = std::stod(args[4]);
    return send_and_print("window.controlClick");
  }

  if (cmd == "control-send") {
    if (args.size() < 3) return usage();
    params["hwnd"] = args[1];
    params["text"] = args[2];
    return send_and_print("window.controlSend");
  }

  if (cmd == "get-pixel") {
    if (args.size() < 3) return usage();
    params["x"] = std::stod(args[1]);
    params["y"] = std::stod(args[2]);
    return send_and_print("screen.getPixel");
  }

  if (cmd == "pixel-search") {
    if (args.size() < 8) return usage();
    params["left"] = std::stod(args[1]);
    params["top"] = std::stod(args[2]);
    params["right"] = std::stod(args[3]);
    params["bottom"] = std::stod(args[4]);
    params["r"] = std::stod(args[5]);
    params["g"] = std::stod(args[6]);
    params["b"] = std::stod(args[7]);
    if (args.size() > 8) params["variation"] = std::stod(args[8]);
    return send_and_print("screen.pixelSearch");
  }

  if (cmd == "capture") {
    if (args.size() < 5) return usage();
    params["left"] = std::stod(args[1]);
    params["top"] = std::stod(args[2]);
    params["right"] = std::stod(args[3]);
    params["bottom"] = std::stod(args[4]);
    return send_and_print("screen.capture");
  }

  if (cmd == "ps") {
    return send_and_print("process.list");
  }

  if (cmd == "kill") {
    if (args.size() < 2) return usage();
    params["pid"] = std::stod(args[1]);
    return send_and_print("process.kill");
  }

  if (cmd == "file-info") {
    if (args.size() < 2) return usage();
    params["path"] = args[1];
    return send_and_print("file.getInfo");
  }

  if (cmd == "file-read") {
    if (args.size() < 2) return usage();
    params["path"] = args[1];
    return send_and_print("file.read");
  }

  if (cmd == "find-regex") {
    if (args.size() > 1) params["title_regex"] = args[1];
    if (args.size() > 2) params["class_regex"] = args[2];
    return send_and_print("window.findRegex");
  }

  if (cmd == "reg-read") {
    if (args.size() < 2) return usage();
    params["path"] = args[1];
    return send_and_print("reg.read");
  }

  if (cmd == "reg-write") {
    if (args.size() < 5) return usage();
    params["path"] = args[1];
    params["name"] = args[2];
    params["type"] = args[3];
    params["data"] = args[4];
    return send_and_print("reg.write");
  }

  if (cmd == "reg-delete") {
    if (args.size() < 2) return usage();
    params["path"] = args[1];
    if (args.size() > 2) params["name"] = args[2];
    return send_and_print("reg.delete");
  }

  if (cmd == "clip-read") return send_and_print("clipboard.read");
  
  if (cmd == "clip-write") {
    if (args.size() < 2) return usage();
    params["text"] = args[1];
    return send_and_print("clipboard.write");
  }

  if (cmd == "svc-list") return send_and_print("service.list");

  if (cmd == "svc-status") {
    if (args.size() < 2) return usage();
    params["name"] = args[1];
    return send_and_print("service.status");
  }

  if (cmd == "svc-control") {
    if (args.size() < 3) return usage();
    params["name"] = args[1];
    params["action"] = args[2];
    return send_and_print("service.control");
  }

  if (cmd == "env-get") return send_and_print("env.get");

  if (cmd == "env-set") {
    if (args.size() < 3) return usage();
    params["name"] = args[1];
    params["value"] = args[2];
    return send_and_print("env.set");
  }

  if (cmd == "wine-drives") return send_and_print("wine.drives");
  if (cmd == "wine-overrides") return send_and_print("wine.overrides");

  if (cmd == "mutex-check") {
    if (args.size() < 2) return usage();
    params["name"] = args[1];
    return send_and_print("sync.checkMutex");
  }

  if (cmd == "mutex-create") {
    if (args.size() < 2) return usage();
    params["name"] = args[1];
    if (args.size() > 2) params["own"] = (args[2] == "true");
    return send_and_print("sync.createMutex");
  }

  if (cmd == "mem-read") {
    if (args.size() < 4) return usage();
    params["pid"] = std::stod(args[1]);
    params["address"] = (double)std::stoull(args[2], nullptr, 0);
    params["size"] = std::stod(args[3]);
    return send_and_print("mem.read");
  }

  if (cmd == "mem-write") {
    if (args.size() < 4) return usage();
    params["pid"] = std::stod(args[1]);
    params["address"] = (double)std::stoull(args[2], nullptr, 0);
    params["data_b64"] = args[3];
    return send_and_print("mem.write");
  }

  if (cmd == "image-match") {
    if (args.size() < 6) return usage();
    params["left"] = std::stod(args[1]);
    params["top"] = std::stod(args[2]);
    params["right"] = std::stod(args[3]);
    params["bottom"] = std::stod(args[4]);
    params["sub_image_b64"] = args[5];
    return send_and_print("image.match");
  }

  if (cmd == "input-hook") {
    if (args.size() < 2) return usage();
    params["enabled"] = (args[1] == "true");
    return send_and_print("input.hook");
  }

  if (cmd == "events-poll") {
    if (args.size() < 2)
      return usage();
    params["snapshot_id"] = args[1];
    if (args.size() > 2 && args[2].rfind("0x", 0) != 0 && args[2].find("--") == std::string::npos)
      params["old_snapshot_id"] = args[2];
    
    for (int i = 1; i < argc; ++i) {
      if (std::string(argv[i]) == "--wait-ms" && i + 1 < argc) {
        params["wait_ms"] = std::stod(argv[i+1]);
      }
    }
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

  if (cmd == "ui-inspect") {
    if (args.size() < 2)
      return usage();
    params["hwnd"] = args[1];
    return send_and_print("ui.inspect");
  }

  if (cmd == "ui-invoke") {
    if (args.size() < 3)
      return usage();
    params["hwnd"] = args[1];
    params["automation_id"] = args[2];
    return send_and_print("ui.invoke");
  }

  if (cmd == "health") {
    return send_and_print("daemon.health");
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
