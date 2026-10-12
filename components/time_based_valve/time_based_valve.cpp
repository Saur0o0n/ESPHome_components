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
                            ? this->now_ms_() - this->endpoint_start_time_ >= this->endpoint_total_duration_
                            : this->is_at_target_();
  if (finished) {
    const bool was_initializing = this->initializing_;
    const auto pending = this->pending_position_;
    this->pending_position_.reset();
    this->initializing_ = false;
    this->endpoint_run_ = false;
    // Keep the integrated estimate: loop latency can cause a small overshoot.
    if (!was_initializing && this->has_built_in_endstop_ &&
        (this->target_position_ == VALVE_OPEN || this->target_position_ == VALVE_CLOSED)) {
      // Match cover.time_based: the drive's own limit switch stops the motor.
      // The relay/action is deliberately left active until another command.
      this->current_operation = VALVE_OPERATION_IDLE;
    } else {
      this->start_direction_(VALVE_OPERATION_IDLE);
    }
    this->publish_state();
    if (was_initializing && pending.has_value())
      this->make_call().set_position(*pending).perform();
    return;
  }

  const uint32_t now = this->now_ms_();
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

  const float pos = *requested;
  if (!std::isfinite(pos)) {
    ESP_LOGW(TAG, "Ignoring non-finite target position");
    return;
  }
  if (this->initializing_) {
    // Preserve a complete reference run, then apply the latest requested target.
    // A repeated command for the reference endpoint must not cause another run.
    if (pos == this->target_position_)
      this->pending_position_.reset();
    else
      this->pending_position_ = pos;
    return;
  }
  if (this->endpoint_run_ && pos == this->target_position_ &&
      (!this->manual_control_ ||
       this->now_ms_() - this->endpoint_start_time_ >= this->endpoint_travel_duration_)) {
    // Repeated endpoint commands must not extend an active overrun indefinitely.
    return;
  }
  this->endpoint_run_ = false;
  this->target_position_ = pos;
  const bool endpoint = pos == VALVE_OPEN || pos == VALVE_CLOSED;
  if (this->manual_control_ && endpoint) {
    // A physical manual control may have moved the drive without our knowledge.
    // Relearn an endpoint by running for a full travel, including on a repeated
    // command in the same direction. Reset the timing anchor, too.
    this->position = pos == VALVE_OPEN ? VALVE_CLOSED : VALVE_OPEN;
    this->start_position_ = this->position;
    this->start_dir_time_ = this->now_ms_();
    this->plan_endpoint_run_(pos);
    this->start_direction_(pos == VALVE_OPEN ? VALVE_OPERATION_OPENING : VALVE_OPERATION_CLOSING);
  } else if (pos == this->position) {
    if (endpoint && (this->has_built_in_endstop_ || this->end_positions_overrun_ > 0.0f)) {
      this->plan_endpoint_run_(pos);
      this->start_direction_(pos == VALVE_OPEN ? VALVE_OPERATION_OPENING : VALVE_OPERATION_CLOSING);
    } else if (this->current_operation != VALVE_OPERATION_IDLE) {
      this->start_direction_(VALVE_OPERATION_IDLE);
    } else {
      return;
    }
  } else {
    if (endpoint)
      this->plan_endpoint_run_(pos);
    this->start_direction_(pos < this->position ? VALVE_OPERATION_CLOSING : VALVE_OPERATION_OPENING);
  }
  // Publish OPENING/CLOSING immediately; save only when the drive stops.
  this->last_publish_time_ = this->now_ms_();
  this->publish_state(this->current_operation == VALVE_OPERATION_IDLE);
}

void TimeBasedValve::plan_endpoint_run_(float position, bool startup) {
  const uint32_t duration = position == VALVE_OPEN ? this->open_duration_ : this->close_duration_;
  this->endpoint_start_time_ = this->now_ms_();
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

void TimeBasedValve::start_direction_(ValveOperation direction) {
  if (direction == this->current_operation && direction != VALVE_OPERATION_IDLE)
    return;

  this->recompute_position_();
  this->stop_prev_trigger_();
  this->current_operation = direction;
  this->start_position_ = this->position;
  this->start_dir_time_ = this->now_ms_();

  Trigger<> *trigger = &this->stop_trigger_;
  if (direction == VALVE_OPERATION_OPENING) {
    this->last_operation_ = direction;
    trigger = &this->open_trigger_;
  } else if (direction == VALVE_OPERATION_CLOSING) {
    this->last_operation_ = direction;
    trigger = &this->close_trigger_;
  }
  this->prev_command_trigger_ = trigger;
  trigger->trigger();
}

void TimeBasedValve::recompute_position_() {
  if (this->current_operation == VALVE_OPERATION_IDLE)
    return;
  const uint32_t elapsed = this->now_ms_() - this->start_dir_time_;
  const bool opening = this->current_operation == VALVE_OPERATION_OPENING;
  const float duration = opening ? this->open_duration_ : this->close_duration_;
  const float distance = elapsed / duration;
  // An absolute movement anchor avoids accumulated float error from thousands
  // of loop iterations. Unsigned subtraction handles millis() wraparound.
  this->position = clamp(this->start_position_ + (opening ? distance : -distance), 0.0f, 1.0f);
}

}  // namespace esphome::time_based_valve
