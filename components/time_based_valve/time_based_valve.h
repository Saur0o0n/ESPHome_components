// SPDX-License-Identifier: GPL-3.0-only
// Adapted from ESPHome 2026.8.2 time_based/cover by Sauron, 2026-10-12.
// Copyright (c) 2019 ESPHome. See LICENSE and NOTICE.
#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/valve/valve.h"

namespace esphome::time_based_valve {

enum class StartPosition : uint8_t { ASIS, CLOSED, OPEN };

// Independent Valve implementation; deliberately not final so valve-specific
// controllers can extend it without depending on Cover or TemplateValve.
class TimeBasedValve : public valve::Valve, public Component {
 public:
  void setup() override;
  // GPIO switches/outputs must be initialized before a startup action runs.
  float get_setup_priority() const override { return setup_priority::DATA - 1.0f; }
  void loop() override;
  void dump_config() override;
  valve::ValveTraits get_traits() override;

  Trigger<> *get_open_trigger() { return &this->open_trigger_; }
  Trigger<> *get_close_trigger() { return &this->close_trigger_; }
  Trigger<> *get_stop_trigger() { return &this->stop_trigger_; }
  void set_open_duration(uint32_t value) { this->open_duration_ = value; }
  void set_close_duration(uint32_t value) { this->close_duration_ = value; }
  void set_has_built_in_endstop(bool value) { this->has_built_in_endstop_ = value; }
  void set_manual_control(bool value) { this->manual_control_ = value; }
  void set_assumed_state(bool value) { this->assumed_state_ = value; }
  void set_start_position(StartPosition value) { this->startup_position_ = value; }
  void set_end_positions_overrun(float value) { this->end_positions_overrun_ = value; }
  bool is_initializing() const { return this->initializing_; }
  valve::ValveOperation get_last_operation() const { return this->last_operation_; }

 protected:
  void control(const valve::ValveCall &call) override;
  virtual uint32_t now_ms_() const;
  virtual void start_direction_(valve::ValveOperation direction);
  void stop_prev_trigger_();
  void recompute_position_();
  bool is_at_target_() const;
  void plan_endpoint_run_(float position, bool startup = false);

  Trigger<> open_trigger_;
  Trigger<> close_trigger_;
  Trigger<> stop_trigger_;
  Trigger<> *prev_command_trigger_{nullptr};
  uint32_t open_duration_{1};
  uint32_t close_duration_{1};
  uint32_t start_dir_time_{0};
  uint32_t last_publish_time_{0};
  float start_position_{0.5f};
  float target_position_{0.5f};
  bool has_built_in_endstop_{false};
  bool manual_control_{false};
  bool assumed_state_{true};
  StartPosition startup_position_{StartPosition::ASIS};
  float end_positions_overrun_{0.0f};
  bool initializing_{false};
  optional<float> pending_position_{};
  bool endpoint_run_{false};
  uint32_t endpoint_start_time_{0};
  uint32_t endpoint_travel_duration_{0};
  uint32_t endpoint_total_duration_{0};
  valve::ValveOperation last_operation_{valve::VALVE_OPERATION_OPENING};
};

}  // namespace esphome::time_based_valve
