#pragma once
// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include <atomic>
#include <string>
#include <vector>
#include <utility>
#include <functional>

namespace wininspect {

  /// Minimal mDNS responder that advertises a TCP service on the LAN.
  ///
  /// Listens on UDP port 5353 (mDNS standard), joins the 224.0.0.251
  /// multicast group, and responds to PTR queries for a given service type.
  ///
  /// Service format: <type>._tcp.local (e.g. "_wininspect._tcp.local")
  class MdnsResponder
  {
  public:
    MdnsResponder();
    ~MdnsResponder();

    /// Start the responder in a background thread.
    /// @param running  Signal to stop (set to false to exit)
    /// @param service_type  DNS-SD service type, e.g. "_wininspect"
    /// @param port  TCP port the service is listening on
    /// @param hostname  Hostname to advertise (machine name)
    /// @param txt_records  Optional key=value pairs for TXT record
    void start(std::atomic<bool>* running, const std::string& service_type, int port,
               const std::string& hostname, const std::string& txt_records = "");

    // Build DNS response packet for a PTR query (public for testing)
    static std::vector<uint8_t> build_ptr_response(const std::vector<uint8_t>& query,
                                                   const std::string& service_type,
                                                   const std::string& instance_name, int port,
                                                   const std::string& hostname,
                                                   const std::string& txt_records);

    // Encode a DNS name (label sequence) into wire format (public for testing)
    static std::vector<uint8_t> encode_name(const std::string& name);

    // Parse a DNS name from wire format, return decoded string and new offset
    static std::pair<std::string, int> decode_name(const std::vector<uint8_t>& packet, int offset);

    /// Discover WinInspect daemons on the LAN via mDNS PTR query.
    /// Returns list of "host:port" strings found. Timeout in seconds.
    static std::vector<std::pair<std::string, int>>
    discover(const std::string& service_type = "_wininspect", int timeout_sec = 2);

  private:
    uintptr_t sock_ = 0;
  };

} // namespace wininspect
