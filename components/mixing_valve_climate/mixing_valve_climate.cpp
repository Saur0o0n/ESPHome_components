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
                                        "waiting_for_heat", "idle", "opening", "closing", "fast_closing"};

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

void ParameterNumber::control(float value) { parent_->update_parameter(parameter_, value); }

bool MixingValveClimate::valid_parameter_(Parameter parameter, float value) const {
  if (!std::isfinite(value))
    return false;
  if (parameter == Parameter::DEADBAND) {
    if (value <= 0 || max_target_temperature_ + value >= max_temperature_)
      return false;
    return deadband_number_ == nullptr ||
           (value >= deadband_number_->traits.get_min_value() && value <= deadband_number_->traits.get_max_value());
  }
  return parameter == Parameter::FAST_CLOSE_OFFSET && value >= fast_close_hysteresis_ && value <= 20;
}

void MixingValveClimate::publish_parameters_() {
  if (deadband_number_ != nullptr) deadband_number_->publish_state(deadband_);
  if (fast_close_offset_number_ != nullptr) fast_close_offset_number_->publish_state(fast_close_offset_);
}

void MixingValveClimate::update_parameter(Parameter parameter, float value) {
  if (!valid_parameter_(parameter, value)) {
    ESP_LOGW(TAG, "Rejected invalid regulation parameter");
    publish_parameters_();
    return;
  }
  float &setting = parameter == Parameter::DEADBAND ? deadband_ : fast_close_offset_;
  if (setting != value) {
    setting = value;
    fast_closing_ = false;  // Evaluate changed thresholds as a new decision.
    if (ready_) {
      if (restore_state_enabled_) {
        Settings settings{deadband_, fast_close_offset_};
        settings_pref_.save(&settings);
      }
      evaluate_();
    }
  }
  publish_parameters_();
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
  // Separate versioned preferences: do not reinterpret climate mode/setpoint storage.
  settings_pref_ = make_entity_preference<Settings>(0x4D564301UL);
  Settings settings{};
  if (restore_state_enabled_ && settings_pref_.load(&settings)) {
    if (valid_parameter_(Parameter::DEADBAND, settings.deadband)) deadband_ = settings.deadband;
    if (valid_parameter_(Parameter::FAST_CLOSE_OFFSET, settings.fast_close_offset)) fast_close_offset_ = settings.fast_close_offset;
  }
  publish_parameters_();
  sensor_->add_on_state_callback([this](float value) { accept_temperature_(value); });
  if (sensor_->has_state()) {
    current_temperature = sensor_->state;
    sensor_received_at_ = now_ms_();
    sensor_seen_ = true;
  }
  ready_ = true;
  publish_heat_source_diagnostics_();
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
  heat_ever_received_ = true;
  heat_age_ms_ = 0;
  heat_age_updated_at_ = heat_received_at_;
  publish_heat_source_diagnostics_();
  if (ready_)
    evaluate_();
}

void MixingValveClimate::invalidate_heat_source() {
  heat_seen_ = false;
  heat_source_ = HeatSource::UNKNOWN;
  publish_heat_source_diagnostics_();
  if (ready_)
    evaluate_();
}

void MixingValveClimate::publish_heat_source_diagnostics_() {
  if (heat_ever_received_) {
    const uint32_t now = now_ms_();
    // Accumulate elapsed intervals so age keeps growing across millis() wraps.
    heat_age_ms_ += uint32_t(now - heat_age_updated_at_);
    heat_age_updated_at_ = now;
  }
  if (heat_source_status_sensor_ != nullptr) {
    const auto state = get_heat_source();
    const char *value = state == HeatSource::AVAILABLE ? "available" :
                        state == HeatSource::UNAVAILABLE ? "unavailable" : "unknown";
    if (!heat_source_status_sensor_->has_state() || heat_source_status_sensor_->state != value)
      heat_source_status_sensor_->publish_state(value);
  }
  if (heat_source_age_sensor_ != nullptr) {
    const float age = heat_ever_received_ ? static_cast<float>(heat_age_ms_ / 1000) : NAN;
    const float previous = heat_source_age_sensor_->state;
    if (!heat_source_age_sensor_->has_state() ||
        (std::isnan(age) != std::isnan(previous)) || (!std::isnan(age) && age != previous))
      heat_source_age_sensor_->publish_state(age);
  }
}

HeatSource MixingValveClimate::get_heat_source() const {
  if (!heat_seen_ || uint32_t(now_ms_() - heat_received_at_) >= heat_source_timeout_)
    return HeatSource::UNKNOWN;
  return heat_source_;
}

void MixingValveClimate::control(const ClimateCall &call) {
  if (call.get_mode().has_value())
    mode = *call.get_mode();
  if (call.get_target_temperature().has_value() && std::isfinite(*call.get_target_temperature())) {
    float target = clamp(*call.get_target_temperature(), min_target_temperature_, max_target_temperature_);
    if (target != target_temperature) fast_closing_ = false;
    target_temperature = target;
  }
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
  publish_heat_source_diagnostics_();
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
  const bool same_closing = (command == Command::CLOSE || command == Command::CLOSE_CONTINUOUS) &&
                           (command_state_ == Command::CLOSE || command_state_ == Command::CLOSE_CONTINUOUS);
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
  if (same_closing && at_endpoint && valve_->current_operation == valve::VALVE_OPERATION_IDLE)
    return;  // A speed change must not repeat a completed closing overrun.
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
    fast_closing_ = false;
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
    fast_closing_ = false;
    command_state_ = Command::NONE;
    report_(Status::INITIALIZING, mode == CLIMATE_MODE_OFF ? CLIMATE_ACTION_OFF : CLIMATE_ACTION_IDLE);
    return;
  }
  if (mode == CLIMATE_MODE_OFF) {
    fast_closing_ = false;
    // OFF relinquishes ownership after stopping once, allowing manual testing.
    if (command_state_ != Command::STOP)
      command_(Command::STOP);
    report_(Status::OFF, CLIMATE_ACTION_OFF);
    return;
  }
  if (!fresh || !std::isfinite(target_temperature)) {
    fast_closing_ = false;
    command_(Command::CLOSE_CONTINUOUS);
    report_(Status::SENSOR_FAULT, CLIMATE_ACTION_COOLING);
    return;
  }
  const float upper = target_temperature + deadband_;
  const float fast_threshold = upper + fast_close_offset_;
  if (fast_closing_) {
    if (current_temperature <= fast_threshold - fast_close_hysteresis_) fast_closing_ = false;
  } else if (current_temperature > fast_threshold) {
    fast_closing_ = true;
  }
  if (current_temperature > upper) {
    command_(fast_closing_ ? Command::CLOSE_CONTINUOUS : Command::CLOSE);
    report_(fast_closing_ ? Status::FAST_CLOSING : Status::CLOSING, CLIMATE_ACTION_COOLING);
  } else if (current_temperature < target_temperature - deadband_) {
    const auto heat = get_heat_source();
    if (heat == HeatSource::UNAVAILABLE || (heat_source_required_ && heat == HeatSource::UNKNOWN)) {
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
  LOG_TEXT_SENSOR("  ", "Heat source status", heat_source_status_sensor_);
  LOG_SENSOR("  ", "Heat source age", heat_source_age_sensor_);
  ESP_LOGCONFIG(TAG, "  Deadband: +/- %.2f C; maximum: %.2f C; reset difference: %.2f C", deadband_,
                max_temperature_, overtemperature_hysteresis_);
  ESP_LOGCONFIG(TAG, "  Fast closing offset: %.2f C; hysteresis: %.2f C", fast_close_offset_, fast_close_hysteresis_);
  ESP_LOGCONFIG(TAG, "  Sensor timeout: %u ms; heat source required: %s; heartbeat timeout: %u ms",
                (unsigned) sensor_timeout_, YESNO(heat_source_required_), (unsigned) heat_source_timeout_);
}
}  // namespace esphome::mixing_valve_climate
