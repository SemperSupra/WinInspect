#pragma once

#include "wininspect/core.hpp"
#include <atomic>
#include <string>

namespace wininspectd {

void run_http_server(std::atomic<bool> *running, int port,
                      wininspect::CoreEngine &core,
                      const std::string &auth_token);

} // namespace wininspectd
