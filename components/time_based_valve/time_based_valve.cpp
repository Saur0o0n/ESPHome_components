// SPDX-License-Identifier: GPL-3.0-only
// Adapted from ESPHome 2026.8.2 time_based/cover by Sauron, 2026-10-12.
// Copyright (c) 2019 ESPHome. See LICENSE and NOTICE.
#include "time_based_valve.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <cmath>

namespace esphome::time_based_valve {

static const char *const TAG = "time_based_valve";
using namespace esphome::valve;

uint32_t TimeBasedValve::now_ms_() const { return millis(); }

void TimeBasedValve::dump_config() {
  LOG_VALVE("", "Time Based Valve", this);
  ESP_LOGCONFIG(TAG, "  Open Duration: %.3fs", this->open_duration_ / 1e3f);
  ESP_LOGCONFIG(TAG, "  Close Duration: %.3fs", this->close_duration_ / 1e3f);
  ESP_LOGCONFIG(TAG, "  Built-in Endstop: %s", YESNO(this->has_built_in_endstop_));
  ESP_LOGCONFIG(TAG, "  Manual Control: %s", YESNO(this->manual_control_));
  ESP_LOGCONFIG(TAG, "  Start Position: %s", this->startup_position_ == StartPosition::ASIS ? "asis" :
                (this->startup_position_ == StartPosition::OPEN ? "open" : "closed"));
  ESP_LOGCONFIG(TAG, "  End Positions Overrun: %.1f%%", this->end_positions_overrun_ * 100.0f);
  ESP_LOGCONFIG(TAG, "  Direction Change Wait: %u ms", this->direction_change_wait_time_);
  ESP_LOGCONFIG(TAG, "  Opening Pulse/Pause: %u/%u ms", this->open_pulse_duration_, this->open_pause_duration_);
  ESP_LOGCONFIG(TAG, "  Closing Pulse/Pause: %u/%u ms", this->close_pulse_duration_, this->close_pause_duration_);
}

void TimeBasedValve::setup() {
  const auto restore = this->restore_state_();
  this->position = restore.has_value() && std::isfinite(restore->position)
                       ? clamp(restore->position, 0.0f, 1.0f)
                       : 0.5f;
  this->current_operation = VALVE_OPERATION_IDLE;
  this->target_position_ = this->position;
  this->start_position_ = this->position;
  this->last_publish_time_ = this->now_ms_();
  if (this->startup_position_ != StartPosition::ASIS) {
    this->initializing_ = true;
    const bool opening = this->startup_position_ == StartPosition::OPEN;
    // Deliberately ignore the restored position. Until the full reference run
    // finishes, this is only a provisional estimate from the opposite endpoint.
    this->position = opening ? VALVE_CLOSED : VALVE_OPEN;
    this->target_position_ = opening ? VALVE_OPEN : VALVE_CLOSED;
    this->plan_endpoint_run_(this->target_position_, true);
    this->start_direction_(opening ? VALVE_OPERATION_OPENING : VALVE_OPERATION_CLOSING);
  }
  this->publish_state(false);
}

ValveTraits TimeBasedValve::get_traits() {
  ValveTraits traits;
  traits.set_supports_stop(true);
  traits.set_supports_position(true);
  traits.set_supports_toggle(true);
  traits.set_is_assumed_state(this->assumed_state_);
  return traits;
}

void TimeBasedValve::loop() {
  if (this->current_operation == VALVE_OPERATION_IDLE)
    return;

  this->recompute_position_();
  const bool finished = this->endpoint_run_
                            ? this->endpoint_powered_time_ >= this->endpoint_total_duration_
                            : this->is_at_target_();
  if (finished) {
    const bool was_initializing = this->initializing_;
    const auto pending = this->pending_position_;
    const auto pending_movement = this->pending_movement_;
    this->pending_position_.reset();
    this->initializing_ = false;
    this->endpoint_run_ = false;
    // Keep the integrated estimate: loop latency can cause a small overshoot.
    if (!was_initializing && this->has_built_in_endstop_ &&
        (this->target_position_ == VALVE_OPEN || this->target_position_ == VALVE_CLOSED)) {
      // Match cover.time_based: the drive's own limit switch stops the motor.
      // The relay/action is deliberately left active until another command.
      this->current_operation = VALVE_OPERATION_IDLE;
      this->movement_ = Movement::CONTINUOUS;
      this->paused_ = false;
    } else {
      this->start_direction_(VALVE_OPERATION_IDLE);
    }
    this->publish_state();
    if (was_initializing && pending.has_value())
      this->control_position(*pending, pending_movement);
    return;
  }

  const uint32_t now = this->now_ms_();
  if (this->direction_change_waiting_) {
    if (now - this->direction_change_started_ < this->direction_change_wait_time_)
      return;
    this->direction_change_waiting_ = false;
    // Start a full pulse at actual action dispatch, even if the loop ran late.
    this->phase_started_ = now;
    this->set_drive_(this->current_operation);
  }
  // Startup and endpoint overrun are continuous. Do not replay missed pulses:
  // a delayed loop accounts for actual powered time, then starts a fresh pause.
  if (this->movement_ == Movement::PULSED && !this->initializing_ && !this->is_overrunning_()) {
    const bool opening = this->current_operation == VALVE_OPERATION_OPENING;
    const uint32_t duration = this->paused_
        ? (opening ? this->open_pause_duration_ : this->close_pause_duration_)
        : (opening ? this->open_pulse_duration_ : this->close_pulse_duration_);
    if (now - this->phase_started_ >= duration) {
      this->paused_ = !this->paused_;
      this->phase_started_ = now;
      this->set_drive_(this->paused_ ? VALVE_OPERATION_IDLE : this->current_operation);
    }
  }
  if (now - this->last_publish_time_ >= 1000) {
    this->last_publish_time_ = now;
    this->publish_state(false);
  }
}

void TimeBasedValve::control(const ValveCall &call) {
  // Account for movement since the last loop before choosing a direction.
  this->recompute_position_();
  if (call.get_stop()) {
    this->initializing_ = false;
    this->pending_position_.reset();
    this->endpoint_run_ = false;
    this->start_direction_(VALVE_OPERATION_IDLE);
    this->publish_state();
    return;
  }

  auto requested = call.get_position();
  if (!requested.has_value() && call.get_toggle().value_or(false)) {
    if (this->current_operation != VALVE_OPERATION_IDLE) {
      this->initializing_ = false;
      this->pending_position_.reset();
      this->endpoint_run_ = false;
      this->start_direction_(VALVE_OPERATION_IDLE);
      this->publish_state();
      return;
    }
    requested = (this->position == VALVE_CLOSED || this->last_operation_ == VALVE_OPERATION_CLOSING)
                    ? VALVE_OPEN
                    : VALVE_CLOSED;
  }
  if (!requested.has_value())
    return;

  this->control_position(*requested, Movement::CONTINUOUS);
}

void TimeBasedValve::control_position(float pos, Movement movement) {
  if (!std::isfinite(pos) || pos < VALVE_CLOSED || pos > VALVE_OPEN ||
      (movement != Movement::CONTINUOUS && movement != Movement::PULSED)) {
    ESP_LOGW(TAG, "Ignoring invalid position or movement mode");
    return;
  }
  this->recompute_position_();
  if (this->initializing_) {
    // Preserve the reference run, and remember both target and movement mode.
    if (pos == this->target_position_)
      this->pending_position_.reset();
    else {
      this->pending_position_ = pos;
      this->pending_movement_ = movement;
    }
    return;
  }
  const bool moving = this->current_operation != VALVE_OPERATION_IDLE;
  const bool same_target = moving && pos == this->target_position_;
  const bool changed_mode = movement != this->movement_;
  if (same_target && (changed_mode || movement == Movement::PULSED ||
      (this->endpoint_run_ && (!this->manual_control_ || this->is_overrunning_())))) {
    // Mode changes preserve travel/overrun already completed. Repeated pulsed
    // commands preserve pulse and pause deadlines, even with manual_control.
    this->set_movement_(movement);
    return;
  }
  this->endpoint_run_ = false;
  this->target_position_ = pos;
  const bool endpoint = pos == VALVE_OPEN || pos == VALVE_CLOSED;
  ValveOperation direction = VALVE_OPERATION_IDLE;
  if (this->manual_control_ && endpoint) {
    // Retain legacy continuous manual-command behavior. Pulsed repeats above
    // are idempotent, but a new endpoint command still requests full travel.
    this->position = pos == VALVE_OPEN ? VALVE_CLOSED : VALVE_OPEN;
    this->start_position_ = this->position;
    this->powered_time_ = 0;
    this->last_recompute_time_ = this->now_ms_();
    this->plan_endpoint_run_(pos);
    direction = pos == VALVE_OPEN ? VALVE_OPERATION_OPENING : VALVE_OPERATION_CLOSING;
  } else if (pos == this->position) {
    if (endpoint && (this->has_built_in_endstop_ || this->end_positions_overrun_ > 0.0f)) {
      this->plan_endpoint_run_(pos);
      direction = pos == VALVE_OPEN ? VALVE_OPERATION_OPENING : VALVE_OPERATION_CLOSING;
    } else if (!moving) {
      return;
    }
  } else {
    if (endpoint)
      this->plan_endpoint_run_(pos);
    direction = pos < this->position ? VALVE_OPERATION_CLOSING : VALVE_OPERATION_OPENING;
  }
  // Set the mode before invoking direction actions. A target change in the
  // same direction preserves an existing pause unless continuous was requested.
  if (direction == this->current_operation)
    this->set_movement_(movement);
  else
    this->movement_ = movement;
  this->start_direction_(direction);
  this->last_publish_time_ = this->now_ms_();
  this->publish_state(this->current_operation == VALVE_OPERATION_IDLE);
}

bool TimeBasedValve::is_overrunning_() const {
  return this->endpoint_run_ && this->endpoint_powered_time_ >= this->endpoint_travel_duration_;
}

void TimeBasedValve::set_movement_(Movement movement) {
  if (movement == this->movement_)
    return;
  this->movement_ = movement;
  this->phase_started_ = this->now_ms_();
  if (this->paused_ && movement == Movement::CONTINUOUS) {
    this->paused_ = false;
    this->set_drive_(this->current_operation);
  }
}

void TimeBasedValve::plan_endpoint_run_(float position, bool startup) {
  const uint32_t duration = position == VALVE_OPEN ? this->open_duration_ : this->close_duration_;
  this->endpoint_powered_time_ = 0;
  this->endpoint_travel_duration_ = static_cast<uint32_t>(std::round(
      static_cast<double>(duration) * std::fabs(position - this->position)));
  const uint32_t extra = startup ? 0 : static_cast<uint32_t>(std::round(
      static_cast<double>(duration) * this->end_positions_overrun_));
  this->endpoint_total_duration_ = this->endpoint_travel_duration_ + extra;
  // With 0% retain the original position-based stopping behavior. Startup must
  // always time a full travel, even if position/state is changed by an observer.
  this->endpoint_run_ = startup || extra > 0;
}

void TimeBasedValve::stop_prev_trigger_() {
  if (this->prev_command_trigger_ != nullptr) {
    this->prev_command_trigger_->stop_action();
    this->prev_command_trigger_ = nullptr;
  }
}

bool TimeBasedValve::is_at_target_() const {
  if (this->current_operation == VALVE_OPERATION_OPENING)
    return this->position >= this->target_position_;
  if (this->current_operation == VALVE_OPERATION_CLOSING)
    return this->position <= this->target_position_;
  return true;
}

void TimeBasedValve::set_drive_(ValveOperation direction, bool force) {
  if (!force && direction == this->drive_direction_)
    return;
  this->recompute_position_();
  this->stop_prev_trigger_();
  this->drive_direction_ = direction;
  Trigger<> *trigger = &this->stop_trigger_;
  if (direction == VALVE_OPERATION_OPENING)
    trigger = &this->open_trigger_;
  else if (direction == VALVE_OPERATION_CLOSING)
    trigger = &this->close_trigger_;
  this->prev_command_trigger_ = trigger;
  trigger->trigger();
}

void TimeBasedValve::start_direction_(ValveOperation direction) {
  if (direction == this->current_operation && direction != VALVE_OPERATION_IDLE)
    return;
  this->recompute_position_();
  // Use the energized direction, not the logical operation: pulse pauses are
  // already stopped, while a built-in endstop may leave a direction energized.
  const bool reversing = direction != VALVE_OPERATION_IDLE &&
      this->drive_direction_ != VALVE_OPERATION_IDLE && direction != this->drive_direction_;
  this->current_operation = direction;
  this->start_position_ = this->position;
  this->powered_time_ = 0;
  this->last_recompute_time_ = this->now_ms_();
  this->phase_started_ = this->last_recompute_time_;
  this->paused_ = false;
  if (direction == VALVE_OPERATION_IDLE) {
    this->movement_ = Movement::CONTINUOUS;
    this->endpoint_run_ = false;
  } else {
    this->last_operation_ = direction;
  }
  if (direction == VALVE_OPERATION_IDLE) {
    this->direction_change_waiting_ = false;
  } else if (this->direction_change_waiting_) {
    // A replacement target/mode uses the original deadline. A stop request
    // cancels this pending transition and returns to ordinary idle.
    return;
  } else if (reversing && this->direction_change_wait_time_ != 0) {
    this->direction_change_waiting_ = true;
    this->direction_change_started_ = this->now_ms_();
    this->set_drive_(VALVE_OPERATION_IDLE, true);
    return;
  }
  this->set_drive_(direction, true);
}

void TimeBasedValve::recompute_position_() {
  const uint32_t now = this->now_ms_();
  const uint32_t elapsed = now - this->last_recompute_time_;
  this->last_recompute_time_ = now;
  if (this->current_operation == VALVE_OPERATION_IDLE || this->drive_direction_ == VALVE_OPERATION_IDLE)
    return;
  this->powered_time_ += elapsed;
  if (this->endpoint_run_)
    this->endpoint_powered_time_ += elapsed;
  const bool opening = this->current_operation == VALVE_OPERATION_OPENING;
  const double duration = opening ? this->open_duration_ : this->close_duration_;
  const float distance = this->powered_time_ / duration;
  this->position = clamp(this->start_position_ + (opening ? distance : -distance), 0.0f, 1.0f);
}

}  // namespace esphome::time_based_valve
