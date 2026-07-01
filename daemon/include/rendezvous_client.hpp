#pragma once
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

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
  int64_t last_seen{};      // unix timestamp
};

/// Pluggable interface for rendezvous-based daemon discovery.
///
/// Implementations:
///   - RendezvousHttp  (Phase 1 — HTTP rendezvous server)
///   - RendezvousNostr (future — Nostr protocol)
///   - RendezvousDht   (future — libp2p Kademlia DHT)
class IRendezvousClient {
public:
  virtual ~IRendezvousClient() = default;

  /// Register this daemon instance with the rendezvous.
  virtual bool register_instance(const wininspect::InstanceIdentity &id,
                                  const std::string &host, int port) = 0;

  /// Send heartbeat to keep registration alive.
  virtual bool heartbeat() = 0;

  /// Deregister on shutdown.
  virtual bool deregister() = 0;

  /// Discover all registered instances.
  virtual std::vector<DiscoveredInstance> discover() = 0;
};

/// Factory: create a rendezvous client from config.
/// Returns nullptr if config is empty or URL is invalid.
std::unique_ptr<IRendezvousClient> create_rendezvous_client(
    const wininspect::RendezvousConfig &cfg);

} // namespace wininspectd
