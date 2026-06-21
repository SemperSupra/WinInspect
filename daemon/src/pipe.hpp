#pragma once
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung


#include <cstdint>
#include <string>
#include <vector>

namespace wininspectd {

struct PipeMessage {
  std::string json;
};

bool pipe_read_message(void *hPipe, PipeMessage &out);
bool pipe_write_message(void *hPipe, const std::string &json);

} // namespace wininspectd
