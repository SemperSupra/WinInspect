#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace wininspectd {

struct PipeMessage {
  std::string json;
};

bool pipe_read_message(void* hPipe, PipeMessage& out);
bool pipe_write_message(void* hPipe, const std::string& json);

} // namespace wininspectd
