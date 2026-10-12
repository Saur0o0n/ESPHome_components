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
  void pulse(float value) { control_position(value, time_based_valve::Movement::PULSED); }
  void target(float value) { make_call().set_position(value).perform(); }
  void identify(uint32_t key) { configure_entity_("fixture", key, 0); }
  void stop() { make_call().set_command_stop().perform(); }
  void toggle() { make_call().set_command_toggle().perform(); }
  void advance(uint32_t ms) { clock += ms; loop(); }
  uint32_t now_ms_() const override { return clock; }
};

inline void run_pulsed() {
  using time_based_valve::Movement;
  using time_based_valve::StartPosition;
  {
    TestValve v(0.2f);
    v.pulse(0.25f); v.advance(2000);
    check(v.is_paused() && !v.open_output && near(v.position, 0.2f + 2.0f/120),
          "opening pulse powers drive for 2 seconds then pauses");
    check(v.current_operation == VALVE_OPERATION_OPENING, "pause retains logical OPENING state");
    const float paused_position = v.position;
    v.advance(7999);
    check(v.is_paused() && v.position == paused_position, "pause never advances estimate");
    v.advance(1); v.advance(2000); v.advance(8000); v.advance(2000);
    check(near(v.position, 0.25f) && v.current_operation == VALVE_OPERATION_IDLE &&
          v.opens == 3 && v.stops == 3, "partial target uses powered time and stops at 22 seconds");
    check(v.get_movement() == Movement::CONTINUOUS && !v.is_paused(), "mode expires with command");
    v.advance(50000);
    check(v.opens == 3 && v.stops == 3, "completed pulsed command cannot restart itself");
  }
  {
    TestValve v(0.5f, 120000, 60000);
    v.set_close_pulse_duration(500); v.set_close_pause_duration(1500);
    v.pulse(0.45f); v.advance(500);
    check(v.is_paused() && near(v.position, 0.5f-0.5f/60), "independent closing pulse and travel times");
    v.advance(1499); check(v.closes == 1, "closing pause has independent duration");
    v.advance(1); check(v.closes == 2 && v.close_output, "closing resumes at configured pause boundary");
    v.stop(); v.advance(10000);
    check(!v.close_output && v.closes == 2, "STOP during pulse cancels future activity");
  }
  {
    TestValve v;
    v.pulse(1); v.advance(2000); v.stop(); v.advance(8000);
    check(v.opens == 1 && !v.open_output && !v.is_paused() &&
          v.current_operation == VALVE_OPERATION_IDLE, "STOP during pause prevents scheduled resume");
    v.toggle(); check(v.close_output && v.get_movement() == Movement::CONTINUOUS,
                     "toggle after pulsed STOP starts normal opposite movement");
  }
  {
    TestValve v;
    v.pulse(1); v.advance(2000); v.toggle(); v.advance(20000);
    check(v.opens == 1 && v.current_operation == VALVE_OPERATION_IDLE,
          "toggle during pause stops the operation rather than resuming");
  }
  for (bool manual : {false, true}) {
    TestValve v;
    v.set_manual_control(manual); v.pulse(1);
    for (int i=0; i<20; ++i) { v.pulse(1); v.advance(100); }
    check(v.is_paused() && v.opens == 1, "repeated pulsed commands cannot extend an active pulse");
    for (int i=0; i<80; ++i) { v.pulse(1); v.advance(100); }
    check(!v.is_paused() && v.opens == 2 && near(v.position, 2.0f/120),
          "repeated pulsed commands cannot extend a pause or reset manual estimate");
  }
  {
    TestValve v;
    v.pulse(1); v.advance(2000); v.advance(3000); v.pulse(0.5f);
    check(v.is_paused() && v.opens == 1, "same-direction target change preserves pause");
    v.advance(4999); check(v.is_paused(), "target change does not shorten pause");
    v.advance(1); check(v.opens == 2, "target change does not extend pause");
    v.clock += 1000; v.pulse(0.025f);
    check(v.current_operation == VALVE_OPERATION_IDLE && !v.open_output,
          "target equal to fresh position stops a pulse");
  }
  {
    TestValve v;
    v.pulse(1); v.advance(2000); v.advance(1000); v.pulse(v.position);
    check(v.current_operation == VALVE_OPERATION_IDLE, "target equal to paused position stops operation");
  }
  for (bool paused : {false, true}) {
    TestValve v(0.5f);
    v.pulse(1); v.advance(paused ? 2000 : 1000);
    const int opens = v.opens;
    v.target(0);
    check(v.closes == 1 && v.opens == opens && v.close_output && !v.is_paused() &&
          v.get_movement() == Movement::CONTINUOUS, "normal reversal cancels pulse without spurious reopening");
    v.advance(120000);
    check(v.position == 0 && !v.close_output, "normal emergency close finishes despite previous pulse");
  }
  {
    TestValve v(0.5f);
    v.pulse(1); v.advance(2000); v.pulse(0);
    check(v.close_output && !v.is_paused(), "pulsed reversal starts closing immediately from pause");
    v.advance(2000); v.advance(3999); check(v.is_paused(), "reversal uses closing pause");
    v.advance(1); check(v.closes == 2, "reversed command resumes in correct direction");
  }
  {
    TestValve v;
    v.pulse(0.5f); v.advance(2000); v.advance(3000); v.target(0.5f);
    check(v.open_output && v.opens == 2 && !v.is_paused(), "normal command immediately resumes paused same target");
    v.advance(58000);
    check(v.current_operation == VALVE_OPERATION_IDLE && near(v.position, 0.5f),
          "continuous conversion excludes earlier pause from position");
  }
  {
    TestValve v;
    v.target(1); v.advance(1000); v.pulse(1); v.advance(1999);
    check(v.open_output && v.opens == 1, "continuous to pulsed starts new pulse window without relay retrigger");
    v.advance(1); check(v.is_paused() && near(v.position, 3.0f/120), "converted pulse integrates previous movement");
    v.advance(8000); v.advance(1000); v.target(1); v.advance(116000);
    check(v.position == 1 && v.current_operation == VALVE_OPERATION_IDLE,
          "mode changes during active pulse preserve total powered travel");
  }
  for (bool opening : {false, true}) {
    TestValve v(opening ? 0.975f : 0.05f, 120000, 60000);
    const float end=opening ? 1.0f : 0.0f;
    const uint32_t pause=opening ? 8000 : 4000;
    const uint32_t extra=opening ? 6000 : 3000;
    v.set_end_positions_overrun(0.05f);
    v.pulse(end); v.advance(2000); v.advance(pause); v.advance(1000);
    check(v.position == end && !v.is_paused(), "reaching endpoint enters uninterrupted overrun");
    for (uint32_t i=0;i<extra-1;++i) { v.pulse(end); v.advance(1); }
    check(v.current_operation != VALVE_OPERATION_IDLE && !v.is_paused(),
          "overrun never pulses and repeated commands do not reset it");
    v.advance(1);
    check(v.current_operation == VALVE_OPERATION_IDLE && !v.open_output && !v.close_output,
          "overrun excludes pause time and uses direction-specific full duration");
    v.pulse(end); v.advance(extra);
    check(v.current_operation == VALVE_OPERATION_IDLE, "pulsed command at endpoint runs only continuous overrun");
  }
  {
    TestValve v(0.975f);
    v.set_end_positions_overrun(0.05f);
    v.pulse(1); v.advance(2000); v.advance(8000); v.target(1); v.advance(6999);
    check(v.current_operation != VALVE_OPERATION_IDLE, "mode change preserves endpoint travel plus overrun budget");
    v.advance(1); check(v.current_operation == VALVE_OPERATION_IDLE, "mode change does not restart overrun budget");
  }
  {
    TestValve v(1);
    v.set_end_positions_overrun(0.05f);
    v.pulse(1); v.advance(2000); v.target(1); v.advance(3999);
    check(v.current_operation != VALVE_OPERATION_IDLE, "mode switch during overrun preserves remaining time");
    v.advance(1); check(!v.open_output, "overrun stops at original deadline after mode change");
    v.pulse(1); v.advance(1000); v.stop(); v.advance(30000);
    check(!v.open_output && v.current_operation == VALVE_OPERATION_IDLE, "STOP cancels pulsed endpoint overrun");
  }
  {
    TestValve v(0.99f);
    v.set_has_built_in_endstop(true); v.set_end_positions_overrun(0.05f);
    v.pulse(1); v.advance(1200); v.advance(6000);
    check(v.current_operation == VALVE_OPERATION_IDLE && v.open_output && !v.is_paused(),
          "pulsed endpoint preserves built-in endstop behavior after overrun");
    v.stop(); check(!v.open_output, "explicit STOP releases legacy endpoint output");
  }
  {
    TestValve v(0.0f,120000,60000);
    v.identify(0x54425680); v.set_start_position(StartPosition::CLOSED); v.setup();
    v.pulse(0.5f); v.advance(30000);
    check(v.close_output && !v.is_paused() && v.is_initializing(), "startup ignores requested pulsed timing");
    v.advance(30000);
    check(!v.is_initializing() && v.open_output && v.get_movement()==Movement::PULSED,
          "startup queues both target and requested movement mode");
    v.advance(2000); check(v.is_paused(), "queued command uses pulse timings");
  }
  {
    TestValve v;
    v.identify(0x54425681); v.set_start_position(StartPosition::OPEN); v.setup();
    v.pulse(0.5f); v.target(0.25f); v.advance(120000); v.advance(3000);
    check(v.close_output && v.get_movement()==Movement::CONTINUOUS,
          "last queued command determines mode after startup");
  }
  {
    TestValve v;
    v.identify(0x54425682); v.set_start_position(StartPosition::CLOSED); v.setup();
    v.pulse(0.5f); v.stop(); v.advance(240000);
    check(v.opens == 0 && v.current_operation == VALVE_OPERATION_IDLE,
          "startup STOP removes pending pulsed command");
  }
  {
    TestValve v;
    v.clock=std::numeric_limits<uint32_t>::max()-999;
    v.pulse(0.05f); v.advance(2000); v.advance(8000); v.advance(2000); v.advance(8000); v.advance(2000);
    check(near(v.position,0.05f) && v.current_operation==VALVE_OPERATION_IDLE,
          "pulse and position accounting survive millis wraparound");
  }
  {
    TestValve v;
    v.clock=std::numeric_limits<uint32_t>::max()-4999;
    v.pulse(0.05f); v.advance(2000); v.advance(8000);
    check(v.open_output && near(v.position,2.0f/120), "pause deadline survives millis wraparound");
  }
  {
    TestValve v;
    v.pulse(0.5f); v.advance(5000);
    check(near(v.position,5.0f/120) && v.is_paused(), "late loop accounts actual powered time, not nominal pulse");
    v.advance(20000);
    check(v.opens==2 && near(v.position,5.0f/120), "late pause does not invent missed pulses or movement");
  }
  {
    TestValve v;
    v.set_open_pulse_duration(1); v.set_open_pause_duration(1); v.pulse(1);
    for(int i=0;i<239999;++i) v.advance(1);
    check(v.position==1 && v.current_operation==VALVE_OPERATION_IDLE && v.opens==120000,
          "integer powered time avoids drift across 120000 pulses");
  }
  {
    TestValve v;
    v.identify(0x54425683);
    auto pref=v.make_entity_preference<ValveRestoreState>(); ValveRestoreState saved{0.2f}; pref.save(&saved);
    v.setup(); v.pulse(1); v.advance(2000);
    TestValve rebooted; rebooted.identify(0x54425683); rebooted.setup();
    check(near(rebooted.position,0.2f) && rebooted.opens==0, "pulse pauses do not save preferences or resume after reboot");
    v.stop();
    TestValve stopped; stopped.identify(0x54425683); stopped.setup();
    check(near(stopped.position,v.position), "explicit STOP saves pulsed position");
  }
  {
    TestValve v;
    v.pulse(1); v.advance(2000);
    v.pulse(NAN); v.pulse(1.1f); v.pulse(-0.1f);
    v.control_position(0.5f,static_cast<Movement>(42)); v.advance(8000);
    check(v.opens==2 && v.get_movement()==Movement::PULSED,
          "invalid dynamic action values leave existing operation untouched");
  }
  {
    TestValve a,b(1);
    a.pulse(1);b.pulse(0);a.advance(2000);b.advance(2000);a.advance(4000);b.advance(4000);
    check(a.is_paused() && !b.is_paused(), "two pulsed instances keep independent schedules");
  }
  puts("PASS: pulsed movement behavioral tests");
}

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

inline void run_time_based_valve_tests() { valve_tests::run(); valve_tests::run_pulsed(); }
