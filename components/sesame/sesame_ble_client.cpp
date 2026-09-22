#include "sesame_ble_client.h"
#include "sesame_component.h"

namespace esphome::sesame_lock {

void SesameBLEClient::set_state(esp32_ble_tracker::ClientState st) {
  if (st != esp32_ble_tracker::ClientState::ESTABLISHED) {
    // The next connection must register for notify again before the cache can go.
    this->node_ready_ = false;
  }
  esp32_ble_client::BLEClientBase::set_state(st);
}

void SesameBLEClient::connect() {
  const auto before = this->state();
  esp32_ble_client::BLEClientBase::connect();
  // A synchronous esp_ble_gattc_open() rejection moves the client straight back to
  // IDLE inside the parent call, so the owner's loop never observes CONNECTING and
  // its retry backoff would be skipped. Report the failed attempt instead.
  if (this->owner_ != nullptr && this->state() == esp32_ble_tracker::ClientState::IDLE &&
      before != esp32_ble_tracker::ClientState::IDLE) {
    this->owner_->on_connect_attempt_failed();
  }
}

bool SesameBLEClient::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                          esp_ble_gattc_cb_param_t *param) {
  if (!esp32_ble_client::BLEClientBase::gattc_event_handler(event, gattc_if, param))
    return false;
  if (this->owner_ != nullptr)
    this->owner_->gattc_event_handler(event, gattc_if, param);
  // Same release rule as ble_client::BLEClient: the descriptor write that needs
  // the cache has completed and no register_for_notify request is outstanding.
  if (!this->services_.empty() && !this->notify_registration_pending() && this->node_ready_)
    this->release_services();
  return true;
}

}  // namespace esphome::sesame_lock
