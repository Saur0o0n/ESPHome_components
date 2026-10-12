// SPDX-License-Identifier: GPL-3.0-only
// Behavioral tests using the real ESPHome Valve/Automation runtime and a
// deterministic clock. No actuator, network or elapsed wall time is needed.
#pragma once
#include "esphome/components/time_based_valve/time_based_valve.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <limits>

namespace valve_tests {
using namespace esphome;
using namespace esphome::valve;

inline void check(bool condition, const char *description) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", description);
    exit(1);
  }
}
inline bool near(float a, float b) { return std::fabs(a - b) < 0.00001f; }

class TestValve : public time_based_valve::TimeBasedValve {
 public:
  uint32_t clock{0};
  int opens{0}, closes{0}, stops{0}, updates{0};
  bool open_output{false}, close_output{false};
  Automation<> open_automation{get_open_trigger()};
  Automation<> close_automation{get_close_trigger()};
  Automation<> stop_automation{get_stop_trigger()};
  LambdaAction<> open_action{[this]() { ++opens; close_output = false; open_output = true; }};
  LambdaAction<> close_action{[this]() { ++closes; open_output = false; close_output = true; }};
  LambdaAction<> stop_action{[this]() { ++stops; open_output = close_output = false; }};

  explicit TestValve(float initial = 0.0f, uint32_t opening = 120000, uint32_t closing = 120000) {
    position = initial;
    set_open_duration(opening);
    set_close_duration(closing);
    open_automation.add_action(&open_action);
    close_automation.add_action(&close_action);
    stop_automation.add_action(&stop_action);
    add_on_state_callback([this]() { ++updates; });
  }
  void target(float value) { make_call().set_position(value).perform(); }
  void identify(uint32_t key) { configure_entity_("fixture", key, 0); }
  void stop() { make_call().set_command_stop().perform(); }
  void toggle() { make_call().set_command_toggle().perform(); }
  void advance(uint32_t ms) { clock += ms; loop(); }
  uint32_t now_ms_() const override { return clock; }
};

inline void run() {
  using time_based_valve::StartPosition;
  for (bool opening : {false, true}) {
    TestValve v;
    v.identify(opening ? 0x54425641 : 0x54425642);
    auto pref = v.make_entity_preference<ValveRestoreState>();
    ValveRestoreState old{opening ? 1.0f : 0.0f};
    pref.save(&old);  // Already stored at the requested endpoint, still run fully.
    v.set_start_position(opening ? StartPosition::OPEN : StartPosition::CLOSED);
    v.set_end_positions_overrun(0.05f);
    v.set_manual_control(true);
    v.set_has_built_in_endstop(true);
    v.setup();
    check(v.is_initializing() && (opening ? v.opens : v.closes) == 1,
          "startup initiates a full reference run despite saved endpoint");
    v.advance(60000);
    v.target(opening ? 1.0f : 0.0f);
    v.advance(59999);
    check(v.stops == 0 && v.is_initializing(), "startup command repetition does not shorten run");
    v.advance(1);
    check(!v.is_initializing() && v.stops == 1 && v.position == (opening ? 1.0f : 0.0f),
          "startup takes exactly full travel, no overrun, always invokes stop");
    TestValve rebooted;
    rebooted.identify(opening ? 0x54425641 : 0x54425642);
    rebooted.setup();
    check(rebooted.position == v.position && rebooted.opens == 0 && rebooted.closes == 0,
          "startup endpoint saved and restored by asis");
  }
  {
    TestValve v(0.5f, 120000, 60000);
    v.identify(0x54425643);
    v.set_start_position(StartPosition::CLOSED);
    v.set_end_positions_overrun(0.05f);
    v.setup();
    v.target(0.25f); v.advance(30000); v.target(0.5f); v.advance(30000);
    check(!v.is_initializing() && v.stops == 1 && v.opens == 1 && v.position == 0.0f,
          "startup uses closing duration and then runs latest queued target");
    v.advance(60000);
    check(v.stops == 2 && near(v.position, 0.5f), "queued partial target reached without overrun");
  }
  {
    TestValve v;
    v.identify(0x54425644);
    v.set_start_position(StartPosition::CLOSED);
    v.setup(); v.target(0.75f); v.advance(10000); v.stop(); v.advance(240000);
    check(!v.is_initializing() && v.stops == 1 && v.opens == 0,
          "STOP cancels startup and queued target");
  }
  for (bool opening : {false, true}) {
    TestValve v(0.5f, 120000, 60000);
    v.set_end_positions_overrun(0.05f);
    const float end = opening ? 1.0f : 0.0f;
    const uint32_t travel = opening ? 60000 : 30000;
    const uint32_t extra = opening ? 6000 : 3000;
    v.target(end); v.advance(travel);
    check(v.stops == 0 && v.position == end && v.current_operation != VALVE_OPERATION_IDLE,
          "endpoint remains moving for extra time based on the direction's full travel");
    for (uint32_t i = 0; i < extra - 1; ++i) { v.target(end); v.advance(1); }
    check(v.stops == 0, "repeated thermostat commands do not shorten overrun");
    v.advance(1);
    check(v.stops == 1 && v.current_operation == VALVE_OPERATION_IDLE,
          "repeated thermostat commands do not extend overrun");
    v.target(end); v.advance(extra - 1);
    check(v.stops == 1, "command at an idle endpoint performs only the extra duration");
    v.advance(1);
    check(v.stops == 2, "repeat endpoint command completes additional duration");
  }
  {
    TestValve v;
    v.set_end_positions_overrun(0.05f);
    v.target(0.5f); v.advance(60000);
    check(v.stops == 1 && near(v.position, 0.5f), "partial target has no overrun");
    v.target(1.0f); v.advance(60000); v.advance(3000); v.target(0.5f); v.advance(60000);
    check(v.stops == 2 && near(v.position, 0.5f), "reversal interrupts overrun and resets time anchor");
    v.target(1.0f); v.advance(60000); v.stop(); v.advance(6000);
    check(v.stops == 3 && v.current_operation == VALVE_OPERATION_IDLE, "STOP cancels overrun");
  }
  {
    TestValve v;
    v.set_end_positions_overrun(0.05f);
    v.target(0.5f); v.advance(30000); v.target(1.0f); v.advance(95999);
    check(v.stops == 0, "changing partial target to endpoint schedules remaining travel plus overrun");
    v.advance(1);
    check(v.stops == 1, "changed endpoint target deadline is correct");
  }
  {
    TestValve v(0.5f);
    v.set_manual_control(true);
    v.set_end_positions_overrun(0.05f);
    v.target(1.0f); v.advance(120000); v.target(1.0f); v.advance(6000);
    check(v.stops == 1, "manual full travel includes overrun without restarting during overrun");
  }
  {
    TestValve v;
    v.set_end_positions_overrun(0.05f);
    v.clock = std::numeric_limits<uint32_t>::max() - 122999;
    v.target(1.0f); v.advance(125999);
    check(v.stops == 0, "overrun spans millis wraparound");
    v.advance(1);
    check(v.stops == 1, "overrun finishes after wraparound even with a late loop");
  }
  {
    TestValve v;
    v.set_end_positions_overrun(0.05f);
    v.set_has_built_in_endstop(true);
    v.target(1.0f); v.advance(126000);
    check(v.stops == 0 && v.open_output && v.current_operation == VALVE_OPERATION_IDLE,
          "legacy built-in endstop behavior is preserved after overrun");
    v.stop();
    check(!v.open_output, "explicit stop still releases output with built-in endstop");
  }
  {
    TestValve v;
    v.identify(0x54425631);
    v.setup();
    check(near(v.position, 0.5f) && v.opens == 0 && v.closes == 0 && v.stops == 0,
          "first boot starts at estimated 50 percent without actuator actions");
    v.target(0.75f); v.advance(30000);
    TestValve rebooted;
    rebooted.identify(0x54425631);
    rebooted.setup();
    check(near(rebooted.position, 0.75f) && rebooted.current_operation == VALVE_OPERATION_IDLE &&
          rebooted.opens == 0 && rebooted.closes == 0, "saved estimate restored without movement");
    rebooted.target(1.0f); rebooted.advance(12000);
    TestValve interrupted;
    interrupted.identify(0x54425631);
    interrupted.setup();
    check(near(interrupted.position, 0.75f), "movement updates do not overwrite saved stop position");
  }
  {
    TestValve v;
    v.identify(0x54425632);
    auto pref = v.make_entity_preference<ValveRestoreState>();
    ValveRestoreState bad{std::numeric_limits<float>::quiet_NaN()};
    pref.save(&bad);
    v.setup();
    check(near(v.position, 0.5f), "non-finite restored estimate falls back to 50 percent");
  }
  {
    TestValve v(0.2f);
    v.target(0.5f);
    check(v.current_operation == VALVE_OPERATION_OPENING && v.opens == 1 && v.updates == 1,
          "opening and state published immediately");
    v.advance(35999);
    check(v.stops == 0, "20 to 50 percent must not stop before 36 seconds");
    v.advance(1);
    check(near(v.position, 0.5f) && v.stops == 1 && !v.open_output &&
          v.current_operation == VALVE_OPERATION_IDLE, "20 to 50 percent stops after 36 seconds");
    v.advance(100000);
    check(v.stops == 1 && near(v.position, 0.5f), "idle does not move or repeat stop");
  }
  {
    TestValve v(0.8f, 120000, 60000);
    v.target(0.3f);
    v.advance(30000);
    check(near(v.position, 0.3f) && v.stops == 1, "independent closing duration");
  }
  {
    TestValve v;
    v.target(1.0f);
    v.clock += 30000;  // no intervening loop
    v.stop();
    check(near(v.position, 0.25f) && v.stops == 1, "stop integrates time since last loop");
    v.stop();
    check(v.stops == 2, "explicit stop also runs while idle");
  }
  {
    TestValve v;
    v.target(1.0f);
    v.advance(30000);
    v.target(0.5f);
    v.advance(30000);
    check(v.opens == 1 && v.stops == 1 && near(v.position, 0.5f),
          "new target in same direction preserves elapsed travel");
  }
  {
    TestValve v;
    v.target(1.0f);
    v.clock += 60000;
    v.target(0.25f);  // must use current 50%, not stale 0%
    check(v.current_operation == VALVE_OPERATION_CLOSING && v.closes == 1 && !v.open_output,
          "reversal uses fresh position and executes close action");
    v.advance(30000);
    check(near(v.position, 0.25f) && v.stops == 1, "reversal reaches new target");
  }
  {
    TestValve v;
    v.target(1.0f);
    v.clock += 30000;
    v.target(0.25f);
    check(v.stops == 1 && near(v.position, 0.25f), "target equal to moving position stops");
    v.target(0.25f);
    check(v.stops == 1 && v.opens == 1, "target equal to idle position is a no-op");
  }
  {
    TestValve v;
    v.toggle(); v.advance(12000); v.toggle();
    check(v.opens == 1 && v.stops == 1 && near(v.position, 0.1f), "toggle open then stop");
    v.toggle(); v.advance(6000); v.toggle();
    check(v.closes == 1 && v.stops == 2, "toggle close then stop");
    v.toggle();
    check(v.opens == 2, "toggle resumes opposite direction");
  }
  {
    TestValve v;
    v.clock = std::numeric_limits<uint32_t>::max() - 999;
    v.target(0.5f); v.advance(60000);
    check(near(v.position, 0.5f) && v.stops == 1, "millis wraparound");
  }
  {
    TestValve v;
    v.target(1.0f);
    for (int i = 0; i < 120000; ++i) v.advance(1);
    check(v.position == 1.0f && v.stops == 1, "no accumulated float drift across 120000 loops");
  }
  {
    TestValve v;
    v.set_manual_control(true);
    v.target(1.0f); v.advance(60000); v.target(1.0f);
    check(v.position == 0.0f, "manual endpoint command resets estimate");
    v.advance(119999);
    check(v.stops == 0, "repeated manual command restarts full duration");
    v.advance(1);
    check(v.position == 1.0f && v.stops == 1, "manual command completes full travel");
    v.target(1.0f); v.advance(120000);
    check(v.opens == 2 && v.stops == 2, "manual endpoint command runs even at endpoint");
    v.target(0.5f); v.advance(60000);
    check(near(v.position, 0.5f), "manual control retains partial positioning");
  }
  {
    TestValve v(0.5f);
    v.set_manual_control(true);
    v.target(1.0f); v.advance(10000); v.target(0.0f); v.advance(120000);
    check(v.position == 0.0f && v.stops == 1, "manual reversal runs full closing travel");
  }
  {
    TestValve v;
    v.set_has_built_in_endstop(true);
    v.target(1.0f); v.advance(120000);
    check(v.stops == 0 && v.open_output && v.current_operation == VALVE_OPERATION_IDLE,
          "built-in endstop skips stop action at full open");
    v.target(1.0f); v.advance(1);
    check(v.opens == 2 && v.stops == 0, "built-in endstop repeats endpoint action");
    v.target(0.5f); v.advance(60000);
    check(v.stops == 1 && !v.close_output, "built-in endstop still stops at partial target");
    v.target(0.0f); v.advance(60000);
    check(v.stops == 1 && v.close_output, "built-in endstop skips stop at full closed");
    v.stop();
    check(v.stops == 2 && !v.close_output, "explicit stop releases endpoint output");
  }
  {
    TestValve v(0.5f);
    v.target(std::numeric_limits<float>::quiet_NaN());
    check(v.opens == 0 && v.closes == 0 && near(v.position, 0.5f), "NaN target ignored");
    auto t = v.get_traits();
    check(t.get_supports_position() && t.get_supports_stop() && t.get_supports_toggle() &&
          t.get_is_assumed_state(), "valve advertises capabilities");
    v.set_assumed_state(false);
    check(!v.get_traits().get_is_assumed_state(), "assumed_state controls trait");
  }
  {
    TestValve first, second(1.0f);
    first.target(1.0f); second.target(0.0f);
    first.advance(12000); second.advance(24000);
    check(near(first.position, 0.1f) && near(second.position, 0.8f), "instances are independent");
  }
  puts("PASS: time_based_valve behavioral tests");
}
}  // namespace valve_tests

inline void run_time_based_valve_tests() { valve_tests::run(); }
