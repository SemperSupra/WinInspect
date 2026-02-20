#pragma once
#include <atomic>
#include <functional>
#include <string>
#include "server_state.hpp"

namespace wininspect {
class IBackend;
} // namespace wininspect

namespace wininspectd {

class TcpServer {
public:
  TcpServer(int port, wininspect::ServerState *state,
            wininspect::IBackend *backend);
  ~TcpServer();

  void start(std::atomic<bool> *running, bool bind_public = false,
             const std::string &auth_keys = "", bool read_only = false);

private:
  int port_;
  wininspect::ServerState *state_;
  wininspect::IBackend *backend_;
};

} // namespace wininspectd
