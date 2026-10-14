// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Sauron
#pragma once
#include "esphome/core/defines.h"
#ifdef USE_MIXING_VALVE_HA_REQUEST
#include "esphome/components/api/api_server.h"
#include "esphome/core/automation.h"
#include "esphome/core/log.h"

namespace esphome::mixing_valve_climate {

// Re-request an existing text-sensor subscription without registering additional
// callbacks. HA sends the current value even when unchanged; a missing entity
// produces no response, so the controller's normal heartbeat timeout can expire.
template<typename... Ts> class RequestHeatSourceAction : public Action<Ts...> {
 public:
  void set_entity_id(const char *entity_id) { entity_id_ = entity_id; }
  void play(const Ts &...x) override {
    auto *server = api::global_api_server;
    if (server == nullptr || !server->is_connected_with_state_subscription()) return;
    api::SubscribeHomeAssistantStateResponse request;
    request.entity_id = StringRef(entity_id_);
    request.once = true;
    for (const auto &client : server->active_clients()) {
      if (!client->is_authenticated() || client->is_marked_for_removal()) continue;
      if (!client->send_message(request))
        ESP_LOGW("mixing_valve_climate", "HA state request could not be queued; retrying next interval");
    }
  }
 protected:
  const char *entity_id_{nullptr};
};

}  // namespace esphome::mixing_valve_climate
#endif
