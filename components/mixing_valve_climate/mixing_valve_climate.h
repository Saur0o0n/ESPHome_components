// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Sauron
#pragma once

#include "esphome/core/component.h"
#include "home_assistant_request.h"
#include "esphome/core/automation.h"
#include "esphome/core/preferences.h"
#include "esphome/components/number/number.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/time_based_valve/time_based_valve.h"

namespace esphome::mixing_valve_climate {

enum class HeatSource : uint8_t { UNKNOWN, AVAILABLE, UNAVAILABLE };
enum class Status : uint8_t { OFF, INITIALIZING, SENSOR_FAULT, OVERTEMPERATURE, WAITING_FOR_HEAT, IDLE, OPENING, CLOSING, FAST_CLOSING };

enum class Parameter : uint8_t { DEADBAND, FAST_CLOSE_OFFSET };
class MixingValveClimate;

class ParameterNumber : public number::Number {
 public:
  ParameterNumber(MixingValveClimate *parent, Parameter parameter) : parent_(parent), parameter_(parameter) {}
 protected:
  void control(float value) override;
  MixingValveClimate *parent_;
  Parameter parameter_;
};

class MixingValveClimate : public climate::Climate, public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA - 2.0f; }
  void set_sensor(sensor::Sensor *value) { sensor_ = value; }
  void set_valve(time_based_valve::TimeBasedValve *value) { valve_ = value; }
  void set_status_sensor(text_sensor::TextSensor *value) { status_sensor_ = value; }
  void set_default_target_temperature(float value) { default_target_temperature_ = value; }
  void set_min_target_temperature(float value) { min_target_temperature_ = value; }
  void set_max_target_temperature(float value) { max_target_temperature_ = value; }
  void set_deadband(float value) { deadband_ = value; }
  void set_fast_close_offset(float value) { fast_close_offset_ = value; }
  void set_fast_close_hysteresis(float value) { fast_close_hysteresis_ = value; }
  void set_deadband_number(ParameterNumber *value) { deadband_number_ = value; }
  void set_fast_close_offset_number(ParameterNumber *value) { fast_close_offset_number_ = value; }
  float get_deadband() const { return deadband_; }
  float get_fast_close_offset() const { return fast_close_offset_; }
  void update_parameter(Parameter parameter, float value);
  void set_max_temperature(float value) { max_temperature_ = value; }
  void set_overtemperature_hysteresis(float value) { overtemperature_hysteresis_ = value; }
  void set_sensor_timeout(uint32_t value) { sensor_timeout_ = value; }
  void set_heat_source_required(bool value) { heat_source_required_ = value; }
  void set_heat_source_timeout(uint32_t value) { heat_source_timeout_ = value; }
  void set_default_mode(climate::ClimateMode value) { default_mode_ = value; }
  void set_restore_state(bool value) { restore_state_enabled_ = value; }
  // Every invocation is a new heartbeat, even when the state has not changed.
  void set_heat_source(const std::string &state);
  HeatSource get_heat_source() const;
  Status get_status() const { return status_; }

 protected:
  enum class Command : uint8_t { NONE, STOP, OPEN, CLOSE, CLOSE_CONTINUOUS };
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;
  virtual uint32_t now_ms_() const;
  void accept_temperature_(float value);
  void evaluate_();
  void command_(Command command);
  void report_(Status status, climate::ClimateAction action);
  bool valid_parameter_(Parameter parameter, float value) const;
  void publish_parameters_();
  struct Settings { float deadband; float fast_close_offset; };
  ESPPreferenceObject settings_pref_;
  ParameterNumber *deadband_number_{nullptr}, *fast_close_offset_number_{nullptr};
  float fast_close_offset_{5}, fast_close_hysteresis_{1};
  bool fast_closing_{false};

  sensor::Sensor *sensor_{nullptr};
  text_sensor::TextSensor *status_sensor_{nullptr};
  time_based_valve::TimeBasedValve *valve_{nullptr};
  float default_target_temperature_{30}, min_target_temperature_{18}, max_target_temperature_{46};
  float deadband_{2}, max_temperature_{50}, overtemperature_hysteresis_{2};
  uint32_t sensor_timeout_{30000}, heat_source_timeout_{90000};
  uint32_t sensor_received_at_{0}, heat_received_at_{0}, last_loop_at_{0};
  bool sensor_seen_{false}, heat_seen_{false}, overtemperature_{false}, ready_{false};
  bool heat_source_required_{false}, restore_state_enabled_{true};
  bool status_published_{false};
  climate::ClimateMode default_mode_{climate::CLIMATE_MODE_OFF};
  HeatSource heat_source_{HeatSource::UNKNOWN};
  Status status_{Status::OFF};
  Command command_state_{Command::NONE};
};

template<typename... Ts> class HeatSourceAction : public Action<Ts...> {
 public:
  explicit HeatSourceAction(MixingValveClimate *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, state)
  void play(const Ts &...x) override { parent_->set_heat_source(state_.value(x...)); }
 protected:
  MixingValveClimate *parent_;
};

}  // namespace esphome::mixing_valve_climate
