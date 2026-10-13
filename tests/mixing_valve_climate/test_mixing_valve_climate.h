// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Sauron
#pragma once
#include "esphome/components/mixing_valve_climate/mixing_valve_climate.h"
#include "esphome/core/preferences.h"
#include <limits>

namespace mixing_tests {
using namespace esphome;
using namespace esphome::climate;
using namespace esphome::mixing_valve_climate;
using valve_tests::check;
using valve_tests::near;
using valve_tests::TestValve;

class TestClimate : public MixingValveClimate {
 public:
  TestValve *clock_valve;
  explicit TestClimate(TestValve *v) : clock_valve(v) {}
  uint32_t now_ms_() const override { return clock_valve->clock; }
  void identify(uint32_t key) { configure_entity_("fixture", key, 0); }
};

struct Fixture {
  TestValve v{0.5f};
  sensor::Sensor sensor;
  TestClimate c{&v};
  explicit Fixture(float temperature = 30, bool remote = false, uint32_t key = 0) {
    c.set_sensor(&sensor); c.set_valve(&v); c.set_restore_state(false);
    c.set_heat_source_required(remote); c.set_default_mode(CLIMATE_MODE_AUTO);
    c.set_sensor_timeout(300000);
    if (key) c.identify(key);
    sensor.publish_state(temperature);
    c.setup();
  }
  void sample(float temperature) { sensor.publish_state(temperature); }
  void advance(uint32_t ms) { v.clock += ms; c.loop(); v.loop(); }
};

inline void run() {
  {
    Fixture f;
    check(f.c.mode == CLIMATE_MODE_AUTO && f.c.action == CLIMATE_ACTION_IDLE, "single target AUTO starts inside band");
    auto traits = f.c.get_traits();
    check(traits.supports_mode(CLIMATE_MODE_AUTO) && traits.supports_mode(CLIMATE_MODE_OFF) &&
          !traits.has_feature_flags(CLIMATE_REQUIRES_TWO_POINT_TARGET_TEMPERATURE), "one setpoint with OFF and AUTO");
    for (float t : {28.0f, 32.0f, 30.0f}) {
      f.sample(t); check(f.c.get_status() == Status::IDLE, "inclusive band boundaries stop regulation");
    }
    f.sample(27.9f);
    check(f.v.open_output && f.v.get_movement() == time_based_valve::Movement::PULSED, "cold starts pulsed opening");
    for (int i=0; i<20; ++i) { f.advance(100); f.sample(27); }
    check(f.v.is_paused() && f.v.opens == 1, "frequent samples do not extend pulse");
    for (int i=0; i<80; ++i) { f.advance(100); f.sample(27); }
    check(f.v.opens == 2 && f.v.open_output, "frequent evaluations do not reset pause");
    f.sample(28);
    check(!f.v.open_output && !f.v.is_paused(), "entering band cancels pending movement");
    f.advance(10000); check(f.v.opens == 2, "no stale resume after entering band");
    f.sample(33); f.advance(2000);
    check(f.v.is_paused(), "hot starts pulsed closing");
    f.advance(4000); check(f.v.closes == 2, "closing uses its faster configured cycle");
    f.sample(27); check(f.v.open_output && !f.v.close_output, "temperature reversal changes direction");
    f.c.make_call().set_target_temperature(25).perform();
    check(f.c.action == CLIMATE_ACTION_IDLE, "setpoint change reevaluates immediately");
  }
  {
    Fixture f(25, true);
    check(f.c.get_status() == Status::WAITING_FOR_HEAT && f.v.opens == 0, "unknown heat source prevents opening");
    f.c.set_heat_source("available"); check(f.v.open_output, "fresh available heartbeat permits opening");
    f.c.set_heat_source("unavailable"); check(!f.v.open_output, "CWU interrupts opening immediately");
    f.sample(35); check(f.v.close_output, "CWU still permits cooling");
    f.sample(25); f.c.set_heat_source("available");
    for (int i=0; i<6; ++i) { f.advance(30000); f.c.set_heat_source("available"); }
    check(f.c.get_heat_source() == HeatSource::AVAILABLE, "unchanged state can be refreshed by heartbeats");
    f.advance(89900); check(f.c.get_heat_source() == HeatSource::AVAILABLE, "heartbeat valid until timeout");
    f.advance(100); check(f.c.get_status() == Status::WAITING_FOR_HEAT && !f.v.open_output,
                         "expired heat source stops opening");
    f.c.set_heat_source("available"); f.c.set_heat_source("bad-value");
    check(f.c.get_heat_source() == HeatSource::UNKNOWN && !f.v.open_output, "invalid dynamic state fails closed for opening");
    f.sample(35); check(f.v.close_output, "unknown source still permits closing");
  }
  {
    Fixture f(25);
    f.c.make_call().set_mode(CLIMATE_MODE_OFF).perform();
    check(!f.v.open_output && f.c.action == CLIMATE_ACTION_OFF, "OFF cancels regulation");
    f.v.target(0.75f); f.advance(1000);
    check(f.v.open_output, "OFF permits explicit manual movement");
    f.c.make_call().set_mode(CLIMATE_MODE_OFF).perform();
    check(!f.v.open_output, "repeated explicit OFF is also STOP");
    f.c.make_call().set_mode(CLIMATE_MODE_AUTO).perform();
    check(f.v.open_output, "AUTO resumes regulation");
    f.c.make_call().set_target_temperature(100).perform();
    check(f.c.target_temperature <= 46, "hard maximum target is enforced");
    f.c.make_call().set_target_temperature(-100).perform();
    check(f.c.target_temperature >= 18, "hard minimum target is enforced");
    float old = f.c.target_temperature;
    f.c.make_call().set_target_temperature(NAN).perform();
    check(f.c.target_temperature == old, "NaN target is ignored");
  }
  {
    TestValve v(0.5f); sensor::Sensor s; TestClimate c(&v);
    c.set_sensor(&s); c.set_valve(&v); c.set_restore_state(false); c.set_default_mode(CLIMATE_MODE_AUTO);
    c.setup();
    check(c.get_status() == Status::SENSOR_FAULT && v.close_output, "no initial measurement prevents opening and closes");
    s.publish_state(30);
    check(c.get_status() == Status::IDLE && !v.close_output, "first real measurement releases sensor fault");
  }
  {
    Fixture f(25); f.c.set_sensor_timeout(30000); f.advance(2000);
    check(f.v.is_paused(), "prepare pause before sensor failure");
    f.sample(NAN);
    check(f.c.get_status() == Status::SENSOR_FAULT && f.v.close_output &&
          f.v.get_movement() == time_based_valve::Movement::CONTINUOUS, "NaN cancels pulses and closes continuously");
    f.advance(8000); check(!f.v.open_output, "sensor fault cannot revive old opening");
    f.sample(30); check(f.c.get_status() == Status::IDLE && !f.v.close_output, "fresh valid sample recovers fault");
    f.advance(29900); check(f.c.get_status() == Status::IDLE, "sample valid before expiry");
    f.advance(100); check(f.c.get_status() == Status::SENSOR_FAULT && std::isnan(f.c.current_temperature),
                          "silent sensor timeout is detected without callbacks");
    f.sample(std::numeric_limits<float>::infinity());
    check(f.c.get_status() == Status::SENSOR_FAULT, "infinite sample is invalid");
  }
  {
    Fixture f(25, true); f.c.set_heat_source("available"); f.advance(2000);
    f.sample(50);
    check(f.c.get_status() == Status::OVERTEMPERATURE && f.v.close_output &&
          f.v.get_movement() == time_based_valve::Movement::CONTINUOUS, "maximum temperature overrides pause");
    f.c.make_call().set_mode(CLIMATE_MODE_OFF).perform(); f.c.set_heat_source("unknown");
    check(f.v.close_output, "protection overrides OFF and source loss");
    f.sample(49); check(f.c.get_status() == Status::OVERTEMPERATURE, "protection reset hysteresis");
    f.sample(NAN); check(f.c.get_status() == Status::OVERTEMPERATURE, "invalid data cannot clear protection");
    f.sample(48); check(f.c.get_status() == Status::OFF && !f.v.close_output, "fresh reset threshold clears protection to OFF");
    f.sample(70); check(f.v.close_output, "overtemperature also activates from OFF");
  }
  {
    Fixture f(30); f.v.set_end_positions_overrun(0.05f); f.sample(50);
    for (int i=0; i<660; ++i) { f.advance(100); f.sample(50); }
    check(f.v.current_operation == valve::VALVE_OPERATION_IDLE && near(f.v.position, 0), "emergency closure includes endpoint overrun");
    int closes = f.v.closes;
    for (int i=0; i<100; ++i) { f.advance(100); f.sample(50); }
    check(f.v.closes == closes, "persistent alarm does not restart completed overrun");
    f.v.target(1); f.advance(100);
    check(!f.v.open_output, "alarm reasserts closing if an external command tries to open");
  }
  for (auto start : {time_based_valve::StartPosition::CLOSED, time_based_valve::StartPosition::OPEN}) {
    TestValve v;
    if (start == time_based_valve::StartPosition::CLOSED) v.set_end_positions_overrun(0.05f);
    v.set_start_position(start); v.setup();
    sensor::Sensor s; s.publish_state(25);
    TestClimate c(&v); c.set_restore_state(false); c.set_default_mode(CLIMATE_MODE_AUTO);
    c.set_sensor(&s); c.set_valve(&v); c.set_sensor_timeout(300000); c.setup();
    check(v.is_initializing() && c.get_status() == Status::INITIALIZING, "controller does not interrupt startup homing");
    s.publish_state(50);
    check(!v.open_output && v.close_output, "overtemperature during startup closes");
    for (int i=0; i<1199; ++i) { v.clock+=100; c.loop(); v.loop(); }
    check(v.close_output, "startup alarm ensures full closing traversal despite unknown position");
    v.clock+=100; c.loop(); v.loop();
    check(!v.close_output, "startup emergency closure finishes");
    v.clock+=100; c.loop(); v.loop();
    check(!v.close_output, "closing startup does not gain an extra endpoint overrun from climate");
  }
  {
    TestValve v; v.set_start_position(time_based_valve::StartPosition::CLOSED); v.setup();
    sensor::Sensor s; s.publish_state(25);
    TestClimate c(&v); c.set_sensor(&s); c.set_valve(&v); c.set_restore_state(false);
    c.set_default_mode(CLIMATE_MODE_AUTO); c.set_sensor_timeout(300000); c.setup();
    v.clock=120000; c.loop(); v.loop(); v.clock+=100; c.loop();
    check(!v.is_initializing() && v.open_output, "regulation starts after homing completes");
  }
  {
    Fixture f(25, true); f.v.clock=UINT32_MAX-1000; f.sample(25); f.c.set_heat_source("available");
    f.advance(2000); check(f.c.get_heat_source() == HeatSource::AVAILABLE, "clock wrap keeps fresh heartbeat");
    f.advance(88000); check(f.c.get_heat_source() == HeatSource::UNKNOWN, "clock wrap preserves heartbeat expiry");
    f.v.clock=UINT32_MAX-1000; f.c.loop();
    check(f.c.get_heat_source() == HeatSource::UNKNOWN, "expired heartbeat cannot revive after full wrap");
  }
  {
    Fixture a(25), b(35);
    a.sample(30);
    check(!a.v.open_output && b.v.close_output, "independent controllers do not interfere");
  }
  {
    constexpr uint32_t key=0xFAC801;
    { Fixture f(30, false, key); f.c.make_call().set_target_temperature(37).set_mode(CLIMATE_MODE_OFF).perform(); }
    global_preferences->sync();
    TestValve v; sensor::Sensor s; s.publish_state(30);
    TestClimate c(&v); c.identify(key); c.set_sensor(&s); c.set_valve(&v); c.setup();
    check(c.mode == CLIMATE_MODE_OFF && near(c.target_temperature,37), "mode and single setpoint survive restart");
    check(c.get_heat_source() == HeatSource::UNKNOWN, "source availability is never restored");
  }
  puts("PASS: mixing climate behavioral tests");
}
}  // namespace mixing_tests
inline void run_mixing_climate_tests() { mixing_tests::run(); }
