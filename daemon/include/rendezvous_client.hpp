#pragma once
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#include <string>

namespace wininspectd {

/// Register this instance with a rendezvous domain.
bool rendezvous_register(const std::string &url, const std::string &crypto_key,
                          const std::string &uuid, const std::string &name,
                          const std::string &host, int port);

/// Send heartbeat to keep registration alive.
bool rendezvous_heartbeat(const std::string &url, const std::string &crypto_key,
                           const std::string &uuid);

/// Deregister from a rendezvous domain.
bool rendezvous_deregister(const std::string &url, const std::string &crypto_key,
                            const std::string &uuid);

} // namespace wininspectd
