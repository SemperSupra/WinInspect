// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung
//
// Control state machine tests. Verifies human/agent/script transitions,
// operation modes, local input detection, and input injection gating.
// The formal TLA+ model (WinInspect_Control.tla) specifies the 12 invariants
// that this test suite covers.

#include "doctest/doctest.h"
#include "wininspect/types.hpp"
#ifdef _MSC_VER
#pragma warning(disable : 4834) // discarding [[nodiscard]] return value
#endif

#ifdef _WIN32
#include "control_manager.hpp"
using namespace wininspect;
using namespace wininspectd;

DOCTEST_TEST_CASE("control: human always wins")
{
  ControlManager cm;
  // Human can take from NONE
  DOCTEST_REQUIRE(cm.take_control(ControllerType::Human, "user-1"));
  auto s = cm.get_status();
  DOCTEST_REQUIRE_EQ(s.at("controller").as_str(), "human");

  // Human releases, script takes, human retakes from SCRIPT
  cm.release_control(ControllerType::Human, "admin");
  DOCTEST_REQUIRE(cm.take_control(ControllerType::Script, "script-1"));
  DOCTEST_REQUIRE(cm.take_control(ControllerType::Human, "user-2"));
  s = cm.get_status();
  DOCTEST_REQUIRE_EQ(s.at("controller").as_str(), "human");

  // Human releases, agent takes, human retakes from AGENT
  cm.release_control(ControllerType::Human, "admin");
  DOCTEST_REQUIRE(cm.take_control(ControllerType::Agent, "agent-1"));
  DOCTEST_REQUIRE(cm.take_control(ControllerType::Human, "user-3"));
  s = cm.get_status();
  DOCTEST_REQUIRE_EQ(s.at("controller").as_str(), "human");
}

DOCTEST_TEST_CASE("control: agent blocked by human")
{
  ControlManager cm;
  cm.take_control(ControllerType::Human, "admin");
  DOCTEST_REQUIRE(!cm.take_control(ControllerType::Agent, "bot"));
}

DOCTEST_TEST_CASE("control: agent blocked in human mode")
{
  ControlManager cm;
  cm.set_operation_mode("human");
  DOCTEST_REQUIRE(!cm.take_control(ControllerType::Agent, "bot"));
  DOCTEST_REQUIRE(!cm.take_control(ControllerType::Script, "script"));
}

DOCTEST_TEST_CASE("control: agent succeeds in auto mode")
{
  ControlManager cm;
  cm.set_operation_mode("auto");
  DOCTEST_REQUIRE(cm.take_control(ControllerType::Agent, "bot"));
  auto s = cm.get_status();
  DOCTEST_REQUIRE_EQ(s.at("controller").as_str(), "agent");
}

DOCTEST_TEST_CASE("control: self release works")
{
  ControlManager cm;
  cm.take_control(ControllerType::Agent, "bot-1");
  DOCTEST_REQUIRE(cm.release_control(ControllerType::Agent, "bot-1"));
  auto s = cm.get_status();
  DOCTEST_REQUIRE_EQ(s.at("controller").as_str(), "none");
}

DOCTEST_TEST_CASE("control: wrong agent cannot release")
{
  ControlManager cm;
  cm.take_control(ControllerType::Agent, "bot-1");
  DOCTEST_REQUIRE(!cm.release_control(ControllerType::Agent, "bot-2"));
}

DOCTEST_TEST_CASE("control: human releases anyone")
{
  ControlManager cm;
  cm.take_control(ControllerType::Script, "script-1");
  DOCTEST_REQUIRE(cm.release_control(ControllerType::Human, "admin"));
  auto s = cm.get_status();
  DOCTEST_REQUIRE_EQ(s.at("controller").as_str(), "none");
}

DOCTEST_TEST_CASE("control: local input releases agent in hybrid mode")
{
  ControlManager cm;
  cm.set_operation_mode("hybrid");
  cm.take_control(ControllerType::Agent, "bot");
  cm.notify_local_input();
  auto s = cm.get_status();
  DOCTEST_REQUIRE_EQ(s.at("controller").as_str(), "none");
  DOCTEST_REQUIRE(s.at("local_input_detected").as_bool());
}

DOCTEST_TEST_CASE("control: can_inject_input in hybrid mode")
{
  ControlManager cm;
  cm.set_operation_mode("hybrid");
  cm.take_control(ControllerType::Agent, "bot");
  DOCTEST_REQUIRE(cm.can_inject_input());
  cm.notify_local_input();
  DOCTEST_REQUIRE(!cm.can_inject_input());
}

DOCTEST_TEST_CASE("control: can_inject_input blocked by human")
{
  ControlManager cm;
  cm.take_control(ControllerType::Human, "admin");
  DOCTEST_REQUIRE(!cm.can_inject_input());
}

DOCTEST_TEST_CASE("control: audit log via log_action")
{
  ControlManager cm;
  cm.log_action("test.method", json::Object{}, true, 5);
  cm.log_action("test.method2", json::Object{}, true, 10);
  auto log = cm.get_audit_log(10);
  DOCTEST_REQUIRE(log.size() >= 2);
  // Verify sequence numbers are strictly increasing
  int64_t prev_seq = -1;
  for (auto& entry : log) {
    auto it = entry.as_obj().find("seq");
    if (it != entry.as_obj().end() && it->second.is_num()) {
      int64_t seq = (int64_t)it->second.as_num();
      DOCTEST_REQUIRE(seq > prev_seq);
      prev_seq = seq;
    }
  }
}

DOCTEST_TEST_CASE("control: audit log bounded")
{
  ControlManager cm;
  cm.set_max_entries(5);
  for (int i = 0; i < 20; i++) {
    cm.take_control(ControllerType::Human, "user-" + std::to_string(i));
  }
  auto log = cm.get_audit_log(100);
  DOCTEST_REQUIRE(log.size() <= 5);
}

DOCTEST_TEST_CASE("control: operation mode transitions")
{
  ControlManager cm;
  cm.set_operation_mode("auto");
  DOCTEST_REQUIRE_EQ(cm.get_operation_mode(), "auto");
  cm.set_operation_mode("hybrid");
  DOCTEST_REQUIRE_EQ(cm.get_operation_mode(), "hybrid");
  cm.set_operation_mode("human");
  DOCTEST_REQUIRE_EQ(cm.get_operation_mode(), "human");
  // Invalid mode should be rejected
  cm.set_operation_mode("invalid");
  DOCTEST_REQUIRE_EQ(cm.get_operation_mode(), "human"); // unchanged
}

DOCTEST_TEST_CASE("control: released state is clean")
{
  ControlManager cm;
  cm.take_control(ControllerType::Agent, "bot");
  cm.release_control(ControllerType::Agent, "bot");
  auto s = cm.get_status();
  DOCTEST_REQUIRE_EQ(s.at("controller").as_str(), "none");
  DOCTEST_REQUIRE_EQ(s.at("controller_id").as_str(), "");
}
#endif
