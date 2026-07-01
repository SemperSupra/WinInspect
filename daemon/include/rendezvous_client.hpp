#pragma once
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#include "wininspect/types.hpp"
#include "wininspect/network_config.hpp"
#include "wininspect/tinyjson.hpp"

#include <memory>
#include <string>
#include <vector>

namespace wininspectd {

/// A discovered daemon instance from a rendezvous query.
struct DiscoveredInstance {
  std::string uuid;
  std::string name;
  std::string host;
  int port{};
  std::string pubkey;
  wininspect::json::Object capabilities;
  int64_t last_seen{};
};

/// Pluggable interface for rendezvous-based daemon discovery.
class IRendezvousClient {
public:
  virtual ~IRendezvousClient() = default;
  virtual bool register_instance(const wininspect::InstanceIdentity &id,
                                  const std::string &host, int port) = 0;
  virtual bool heartbeat() = 0;
  virtual bool deregister() = 0;
  virtual std::vector<DiscoveredInstance> discover() = 0;
};

/// Factory: create a rendezvous client from config.
std::unique_ptr<IRendezvousClient> create_rendezvous_client(
    const wininspect::RendezvousConfig &cfg);

/// Simple HTTP rendezvous client functions.
bool rendezvous_register(const std::string &url, const std::string &crypto_key,
                          const std::string &uuid, const std::string &name,
                          const std::string &host, int port);
bool rendezvous_heartbeat(const std::string &url, const std::string &crypto_key,
                           const std::string &uuid);
bool rendezvous_deregister(const std::string &url, const std::string &crypto_key,
                            const std::string &uuid);

} // namespace wininspectd
