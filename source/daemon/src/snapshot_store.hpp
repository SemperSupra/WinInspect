#pragma once
// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include "server_state.hpp"

#include <optional>
#include <string>
#include <utility>

namespace wininspectd {

  // Store one snapshot while preserving both the configured capacity and
  // pinned-snapshot protection. The caller MUST hold st->snapshots_mu so that
  // insertion can be composed atomically with session/subscription updates.
  //
  // Unlike the former post-insert eviction loop, this chooses an evictable
  // resident before inserting. If every resident snapshot is pinned (or the
  // configured capacity is zero), allocation fails closed and no dead snapshot
  // id is returned.
  inline std::optional<std::string> store_snapshot_bounded_locked(
      wininspect::ServerState* st, wininspect::Snapshot snapshot)
  {
    if (!st || st->max_snapshots == 0)
      return std::nullopt;

    const std::string sid = "s-" + std::to_string(st->snap_counter);
    if (st->snaps.count(sid) != 0)
      return std::nullopt;

    while (st->snaps.size() >= st->max_snapshots) {
      auto victim = st->lru_order.end();
      for (auto it = st->lru_order.begin(); it != st->lru_order.end(); ++it) {
        if (st->snaps.count(*it) == 0)
          continue;
        auto pin_it = st->pinned_counts.find(*it);
        const int pin_count = pin_it == st->pinned_counts.end() ? 0 : pin_it->second;
        if (pin_count <= 0) {
          victim = it;
          break;
        }
      }

      if (victim == st->lru_order.end())
        return std::nullopt;

      const std::string evicted = *victim;
      st->lru_order.erase(victim);
      st->snaps.erase(evicted);
      st->pinned_counts.erase(evicted);
      st->evicted_snaps.insert(evicted);
    }

    st->snaps.emplace(sid, std::make_shared<wininspect::Snapshot>(std::move(snapshot)));
    st->lru_order.push_back(sid);
    st->evicted_snaps.erase(sid);
    ++st->snap_counter;
    return sid;
  }

} // namespace wininspectd
