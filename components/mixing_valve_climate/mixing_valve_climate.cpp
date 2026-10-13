// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Sauron
#include "mixing_valve_climate.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <cmath>

namespace esphome::mixing_valve_climate {
using namespace climate;
static const char *const TAG = "mixing_valve_climate";
static const char *const STATUS_NAMES[] = {"off", "initializing", "sensor_fault", "overtemperature",
                                        "waiting_for_heat", "idle", "opening", "closing"};

uint32_t MixingValveClimate::now_ms_() const { return millis(); }

ClimateTraits MixingValveClimate::traits() {
  ClimateTraits result;
  result.add_feature_flags(CLIMATE_SUPPORTS_CURRENT_TEMPERATURE | CLIMATE_SUPPORTS_ACTION);
  result.set_supported_modes({CLIMATE_MODE_OFF, CLIMATE_MODE_AUTO});
  result.set_visual_min_temperature(min_target_temperature_);
  result.set_visual_max_temperature(max_target_temperature_);
  result.set_visual_target_temperature_step(1);
  result.set_visual_current_temperature_step(0.1f);
  return result;
}

void MixingValveClimate::setup() {
  mode = default_mode_;
  target_temperature = default_target_temperature_;
  // Initialize preference storage even if restoring is disabled.
  auto restored = restore_state_();
  if (restore_state_enabled_ && restored.has_value()) {
    if (restored->mode == CLIMATE_MODE_OFF || restored->mode == CLIMATE_MODE_AUTO)
      mode = restored->mode;
    if (std::isfinite(restored->target_temperature))
      target_temperature = clamp(restored->target_temperature, min_target_temperature_, max_target_temperature_);
  }
  sensor_->add_on_state_callback([this](float value) { accept_temperature_(value); });
  if (sensor_->has_state()) {
    current_temperature = sensor_->state;
    sensor_received_at_ = now_ms_();
    sensor_seen_ = true;
  }
  ready_ = true;
  evaluate_();
  publish_state();
}

void MixingValveClimate::accept_temperature_(float value) {
  current_temperature = value;
  sensor_received_at_ = now_ms_();
  sensor_seen_ = true;
  if (ready_) {
    evaluate_();
    publish_state();
  }
}

void MixingValveClimate::set_heat_source(const std::string &state) {
  heat_source_ = state == "available" ? HeatSource::AVAILABLE :
                 state == "unavailable" ? HeatSource::UNAVAILABLE : HeatSource::UNKNOWN;
  heat_received_at_ = now_ms_();
  heat_seen_ = true;
  if (ready_)
    evaluate_();
}

HeatSource MixingValveClimate::get_heat_source() const {
  if (!heat_seen_ || uint32_t(now_ms_() - heat_received_at_) >= heat_source_timeout_)
    return HeatSource::UNKNOWN;
  return heat_source_;
}

void MixingValveClimate::control(const ClimateCall &call) {
  if (call.get_mode().has_value())
    mode = *call.get_mode();
  if (call.get_target_temperature().has_value() && std::isfinite(*call.get_target_temperature()))
    target_temperature = clamp(*call.get_target_temperature(), min_target_temperature_, max_target_temperature_);
  if (ready_) {
    // An explicit OFF also stops a manual move, except startup homing and protection.
    if (call.get_mode() == CLIMATE_MODE_OFF && !valve_->is_initializing() && !overtemperature_) {
      valve_->make_call().set_command_stop().perform();
      command_state_ = Command::STOP;
    }
    evaluate_();
    publish_state();
  }
}

void MixingValveClimate::loop() {
  if (uint32_t(now_ms_() - last_loop_at_) < 100)
    return;
  last_loop_at_ = now_ms_();
  // Expiry is sticky until a new measurement/heartbeat arrives, including
  // after a complete millis() wrap with no updates.
  if (heat_seen_ && uint32_t(now_ms_() - heat_received_at_) >= heat_source_timeout_)
    heat_seen_ = false;
  if (sensor_seen_ && uint32_t(now_ms_() - sensor_received_at_) >= sensor_timeout_) {
    sensor_seen_ = false;
    current_temperature = NAN;
    publish_state();
  }
  if (ready_)
    evaluate_();
}

void MixingValveClimate::command_(Command command) {
  // Issue transitions once. Never re-trigger a completed endpoint overrun.
  bool changed = command != command_state_;
  command_state_ = command;
  if (command == Command::STOP) {
    if (changed || valve_->current_operation != valve::VALVE_OPERATION_IDLE)
      valve_->make_call().set_command_stop().perform();
    return;
  }
  const bool opening = command == Command::OPEN;
  const auto direction = opening ? valve::VALVE_OPERATION_OPENING : valve::VALVE_OPERATION_CLOSING;
  const auto movement = command == Command::CLOSE_CONTINUOUS ? time_based_valve::Movement::CONTINUOUS :
                                                             time_based_valve::Movement::PULSED;
  const bool at_endpoint = opening ? valve_->position >= 1.0f : valve_->position <= 0.0f;
  const bool wrong_motion = valve_->current_operation != direction || valve_->get_movement() != movement;
  if (changed || (wrong_motion && (!at_endpoint || valve_->current_operation != valve::VALVE_OPERATION_IDLE)))
    valve_->control_position(opening ? 1.0f : 0.0f, movement);
}

void MixingValveClimate::report_(Status status, ClimateAction new_action) {
  if (!status_published_ || status_ != status) {
    status_ = status;
    status_published_ = true;
    if (status_sensor_ != nullptr)
      status_sensor_->publish_state(STATUS_NAMES[static_cast<unsigned>(status)]);
    ESP_LOGD(TAG, "State: %s", STATUS_NAMES[static_cast<unsigned>(status)]);
  }
  if (action != new_action) {
    action = new_action;
    publish_state();
  }
}

void MixingValveClimate::evaluate_() {
  const bool fresh = sensor_seen_ && std::isfinite(current_temperature) &&
                     uint32_t(now_ms_() - sensor_received_at_) < sensor_timeout_;
  if (fresh) {
    if (current_temperature >= max_temperature_)
      overtemperature_ = true;
    else if (current_temperature <= max_temperature_ - overtemperature_hysteresis_)
      overtemperature_ = false;
  }
  // Protection has priority over OFF, source availability and startup homing.
  if (overtemperature_) {
    if (valve_->is_initializing()) {
      if (valve_->current_operation == valve::VALVE_OPERATION_CLOSING) {
        command_state_ = Command::CLOSE_CONTINUOUS;
        report_(Status::OVERTEMPERATURE, CLIMATE_ACTION_COOLING);
        return;  // A full closing calibration is already in progress.
      }
      valve_->make_call().set_command_stop().perform();
      // An interrupted opening calibration has no known position. Reserve a
      // complete closing traversal, rather than trusting its partial estimate.
      valve_->position = 1.0f;
    }
    command_(Command::CLOSE_CONTINUOUS);
    report_(Status::OVERTEMPERATURE, CLIMATE_ACTION_COOLING);
    return;
  }
  if (valve_->is_initializing()) {
    command_state_ = Command::NONE;
    report_(Status::INITIALIZING, mode == CLIMATE_MODE_OFF ? CLIMATE_ACTION_OFF : CLIMATE_ACTION_IDLE);
    return;
  }
  if (mode == CLIMATE_MODE_OFF) {
    // OFF relinquishes ownership after stopping once, allowing manual testing.
    if (command_state_ != Command::STOP)
      command_(Command::STOP);
    report_(Status::OFF, CLIMATE_ACTION_OFF);
    return;
  }
  if (!fresh || !std::isfinite(target_temperature)) {
    command_(Command::CLOSE_CONTINUOUS);
    report_(Status::SENSOR_FAULT, CLIMATE_ACTION_COOLING);
    return;
  }
  if (current_temperature > target_temperature + deadband_) {
    command_(Command::CLOSE);
    report_(Status::CLOSING, CLIMATE_ACTION_COOLING);
  } else if (current_temperature < target_temperature - deadband_) {
    if (heat_source_required_ && get_heat_source() != HeatSource::AVAILABLE) {
      command_(Command::STOP);
      report_(Status::WAITING_FOR_HEAT, CLIMATE_ACTION_IDLE);
    } else {
      command_(Command::OPEN);
      report_(Status::OPENING, CLIMATE_ACTION_HEATING);
    }
  } else {
    command_(Command::STOP);
    report_(Status::IDLE, CLIMATE_ACTION_IDLE);
  }
}

void MixingValveClimate::dump_config() {
  LOG_CLIMATE("", "Mixing Valve Climate", this);
  LOG_SENSOR("  ", "Mixed water temperature", sensor_);
  ESP_LOGCONFIG(TAG, "  Deadband: +/- %.2f C; maximum: %.2f C; reset difference: %.2f C", deadband_,
                max_temperature_, overtemperature_hysteresis_);
  ESP_LOGCONFIG(TAG, "  Sensor timeout: %u ms; heat source required: %s; heartbeat timeout: %u ms",
                (unsigned) sensor_timeout_, YESNO(heat_source_required_), (unsigned) heat_source_timeout_);
}
}  // namespace esphome::mixing_valve_climate
