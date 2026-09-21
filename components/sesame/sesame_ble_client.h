#pragma once

#include <esphome/components/esp32_ble_client/ble_client_base.h>

namespace esphome::sesame_lock {

class SesameComponent;

// GATT client owned by one SesameComponent.
//
// esp32_ble_client::BLEClientBase owns the connection lifecycle: GATT app
// registration, address-type learning from advertisements, connect/disconnect
// state machine and CLOSE_EVT recovery. A SESAME device has exactly one
// consumer, so this class only forwards GATT events to the owner and releases
// the peer's GATT cache once the notify descriptor write is done.
class SesameBLEClient : public esp32_ble_client::BLEClientBase {
 public:
  void set_owner(SesameComponent *owner) { this->owner_ = owner; }

  // Set by the owner when RX notifications are enabled, which is the last
  // point the peer's service cache is needed.
  void set_node_ready(bool ready) { this->node_ready_ = ready; }

  bool gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;
  void set_state(esp32_ble_tracker::ClientState st) override;

 private:
  SesameComponent *owner_{nullptr};
  bool node_ready_{false};
};

}  // namespace esphome::sesame_lock
