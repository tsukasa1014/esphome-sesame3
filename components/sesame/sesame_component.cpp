#include "sesame_component.h"
#include <esphome/components/esp32_ble/ble.h>
#include <esphome/core/application.h>
#include <esphome/core/log.h>
#include <libsesame3bt/ServerCore.h>
#include <libsesame3bt/util.h>
#include <algorithm>

#if defined(CONFIG_BT_NIMBLE_ENABLED) && CONFIG_BT_NIMBLE_ENABLED
#error "sesame requires ESPHome standard BLE; remove NimBLE sdkconfig options"
#endif
#if LIBSESAME3BTCORE_DEBUG
#error "Disable LIBSESAME3BTCORE_DEBUG: protocol debug can expose decrypted payloads"
#endif

using libsesame3bt::Sesame;
using libsesame3bt::core::result_t;
using esphome::esp32_ble_tracker::ClientState;
using esphome::esp32_ble::ESPBTUUID;

namespace esphome::sesame_lock {

SesameComponent::SesameComponent(const char* id) : log_tag_string(id) {
  TAG = log_tag_string.c_str();
}

void SesameComponent::init(Sesame::model_t model, std::string_view pubkey,
                           std::string_view secret, std::string_view btaddr, std::string_view uuid) {
  uint64_t address = 0;
  std::string hex;
  auto input = btaddr.empty() ? uuid : btaddr;
  for (char c : input) {
    if (c != '-' && c != ':') hex.push_back(c);
  }
  if (!btaddr.empty()) {
    std::byte bytes[6];
    if (!libsesame3bt::core::util::hex2bin(hex, bytes)) {
      mark_failed();
      return;
    }
    for (auto b : bytes) address = (address << 8) | std::to_integer<uint8_t>(b);
  } else {
    std::byte bytes[16];
    if (!libsesame3bt::core::util::hex2bin(hex, bytes)) {
      mark_failed();
      return;
    }
    // Core's UUID and returned address are both big endian. Do not use NimBLE byte order.
    for (auto b : libsesame3bt::core::SesameServerCore::uuid_to_ble_address(bytes))
      address = (address << 8) | std::to_integer<uint8_t>(b);
  }
  if (!address || sesame.begin(model) != result_t::success || sesame.set_keys(pubkey, secret) != result_t::success) {
    ESP_LOGE(TAG, "Invalid device configuration or keys");
    mark_failed();
    return;
  }
  ble_client_->set_address(address);
  // Advertisements supply the actual address type before the tracker connects.
  ble_client_->set_remote_addr_type(BLE_ADDR_TYPE_RANDOM);
  sesame.set_status_callback([this](auto&, const auto& status) {
    sesame_status = status;
    status_pending_ = true;
  });
}

void SesameComponent::setup() {
  if (feature) feature->publish_initial_state();
  publish_connection_state(false);
}

void SesameComponent::set_state(state_t next) {
  if (my_state == next) return;
  my_state = next;
  state_started = millis();
}

void SesameComponent::reset_session_() {
  sesame.on_disconnected();
  tx_head_ = tx_count_ = 0;
  write_pending_ = subscribed_ = status_pending_ = transport_failed_ = false;
  tx_handle_ = rx_handle_ = cccd_handle_ = 0;
  ble_client_->set_node_ready(false);
  // Queue may contain authentication material; clear it even though it is no longer scheduled.
  for (auto& fragment : tx_queue_) fragment = {};
  make_unknown();
  publish_connection_state(false);
}

void SesameComponent::schedule_retry_() {
  ble_client_->set_auto_connect(false);
  reset_session_();
  if (connect_tried < UINT16_MAX) ++connect_tried;
  uint32_t base = 3000U << std::min<unsigned>(connect_tried - 1, 4);
  retry_delay_ = std::min<uint32_t>(base, 60000) + (ble_client_->get_address() % 997);
  // Preserve the option as a retry-burst limit; never reboot every component for one missing lock.
  if (connect_limit && connect_tried >= connect_limit) {
    retry_delay_ = 60000;
    connect_tried = 0;
  }
  retry_started_ = millis();
  set_state(state_t::not_connected);
}

void SesameComponent::disconnect() {
  ble_client_->set_auto_connect(false);
  reset_session_();
  set_state(state_t::wait_disconnected);
  ble_client_->disconnect();
}

// The parent client only guards DISCONNECTING. If the controller accepted an open
// but never delivered OPEN/CONNECT/CLOSE, the client stays in CONNECTING with no
// conn_id and neither disconnect() nor unconditional_disconnect() can free it.
// Dropping it back to IDLE is what lets the tracker resume scanning and retry.
bool SesameComponent::force_idle_if_unopened_() {
  const auto client_state = ble_client_->state();
  if (client_state != ClientState::CONNECTING && client_state != ClientState::DISCOVERED)
    return false;
  if (ble_client_->get_conn_id() != esp32_ble_client::UNSET_CONN_ID)
    return false;
  ESP_LOGW(TAG, "Link never opened; resetting the BLE client so scanning can resume");
  ble_client_->set_state(ClientState::IDLE);  // also clears the pending disconnect
  return true;
}

bool SesameComponent::write_to_tx(const uint8_t* data, size_t size) {
  if (!tx_handle_ || transport_failed_ || size == 0 || size > 20 || tx_count_ == TX_QUEUE_SIZE ||
      ble_client_->state() != ClientState::ESTABLISHED) {
    transport_failed_ = true;
    return false;
  }
  auto& fragment = tx_queue_[(tx_head_ + tx_count_) % TX_QUEUE_SIZE];
  std::copy_n(data, size, fragment.data.begin());
  fragment.size = size;
  ++tx_count_;
  return true;
}

void SesameComponent::pump_tx_() {
  if (write_pending_ || tx_count_ == 0 || transport_failed_) return;
  auto& fragment = tx_queue_[tx_head_];
  auto rc = esp_ble_gattc_write_char(ble_client_->get_gattc_if(), ble_client_->get_conn_id(),
                                    tx_handle_, fragment.size, fragment.data.data(),
                                    ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (rc != ESP_OK) {
    ESP_LOGW(TAG, "BLE write could not be queued (%d)", rc);
    transport_failed_ = true;
    return;
  }
  write_pending_ = true;
  write_started_ = millis();
}

void SesameComponent::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t,
                                          esp_ble_gattc_cb_param_t* param) {
  // BLEClientBase filters by GATT interface, peer and connection before dispatching here.
  switch (event) {
    case ESP_GATTC_SEARCH_CMPL_EVT: {
      if (param->search_cmpl.status != ESP_GATT_OK || my_state == state_t::wait_disconnected) {
        transport_failed_ = true;
        break;
      }
      const auto service = ESPBTUUID::from_raw(Sesame::SESAME3_SRV_UUID);
      auto* tx = ble_client_->get_characteristic(service, ESPBTUUID::from_raw(Sesame::TxUUID));
      auto* rx = ble_client_->get_characteristic(service, ESPBTUUID::from_raw(Sesame::RxUUID));
      if (!tx || !rx || !(tx->properties & ESP_GATT_CHAR_PROP_BIT_WRITE_NR) ||
          !(rx->properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY)) {
        ESP_LOGW(TAG, "SESAME TX/RX characteristics missing or unsupported");
        transport_failed_ = true;
        break;
      }
      auto* cccd = rx->get_descriptor(ESP_GATT_UUID_CHAR_CLIENT_CONFIG);
      if (!cccd) {
        ESP_LOGW(TAG, "SESAME notification descriptor missing");
        transport_failed_ = true;
        break;
      }
      tx_handle_ = tx->handle;
      rx_handle_ = rx->handle;
      cccd_handle_ = cccd->handle;
      set_state(state_t::authenticating);
      // Parent writes CCCD after registration. Keep its cache until that write completes.
      if (ble_client_->register_for_notify(rx_handle_) != ESP_OK) transport_failed_ = true;
      break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
      if (param->reg_for_notify.handle == rx_handle_ && param->reg_for_notify.status != ESP_GATT_OK)
        transport_failed_ = true;
      break;
    case ESP_GATTC_WRITE_DESCR_EVT:
      if (param->write.handle == cccd_handle_) {
        if (param->write.status != ESP_GATT_OK) {
          transport_failed_ = true;
        } else {
          subscribed_ = true;
          ble_client_->set_node_ready(true);
        }
      }
      break;
    case ESP_GATTC_NOTIFY_EVT:
      if (param->notify.handle == rx_handle_ && param->notify.is_notify &&
          (my_state == state_t::authenticating || my_state == state_t::running) && !transport_failed_) {
        // The initial token may arrive before the local CCCD completion event.
        auto result = sesame.on_received(reinterpret_cast<const std::byte*>(param->notify.value),
                                         param->notify.value_len);
        if (result != result_t::success) {
          ESP_LOGW(TAG, "SESAME protocol error (%u)", static_cast<unsigned>(result));
          transport_failed_ = true;
        }
      }
      break;
    case ESP_GATTC_WRITE_CHAR_EVT:
      if (param->write.handle == tx_handle_ && write_pending_) {
        if (param->write.status != ESP_GATT_OK) {
          transport_failed_ = true;
        } else {
          tx_queue_[tx_head_] = {};
          tx_head_ = (tx_head_ + 1) % TX_QUEUE_SIZE;
          --tx_count_;
          write_pending_ = false;
        }
      }
      break;
    case ESP_GATTC_DISCONNECT_EVT:
    case ESP_GATTC_CLOSE_EVT:
      if (my_state != state_t::not_connected && my_state != state_t::wait_disconnected) {
        reset_session_();
        ble_client_->set_auto_connect(false);
        set_state(state_t::wait_disconnected);
      }
      break;
    default:
      break;
  }
}

void SesameComponent::loop() {
  const auto now = millis();
  const auto client_state = ble_client_->state();
  if (feature) feature->loop();

  if (ble_restart_pending_) {
    // ESP32BLE::enable() only acts on DISABLED/DISABLE, and a failed bring-up
    // leaves the stack OFF with its loop disabled, so retrying forever cannot
    // recover. Keep asking for a while, then restart the whole node.
    if (now - ble_restart_started_ > BLE_RESTART_RETRY_MS) {
      ble_restart_started_ = now;
      if (esp32_ble::global_ble != nullptr)
        esp32_ble::global_ble->enable();
      if (++ble_restart_attempts_ > MAX_BLE_RESTART_ATTEMPTS) {
        ESP_LOGE(TAG, "BLE stack did not come back; rebooting");
        App.safe_reboot();
        return;
      }
    }
    if (esp32_ble::global_ble != nullptr && esp32_ble::global_ble->is_active() &&
        client_state != ClientState::INIT) {
      ble_restart_pending_ = false;
      ble_restart_attempts_ = 0;
      ESP_LOGW(TAG, "BLE stack is back after repeated connection stalls");
    }
    return;
  }

  // A late CONNECT_EVT can set conn_id after the client was dropped back to IDLE,
  // which would leave the controller link open with nothing tracking it.
  if (client_state == ClientState::IDLE && ble_client_->get_conn_id() != esp32_ble_client::UNSET_CONN_ID) {
    ESP_LOGW(TAG, "Closing a link that outlived the client reset");
    ble_client_->unconditional_disconnect();
  }

  // Also handles stack disable/re-enable and the parent's lost CLOSE_EVT recovery.
  if ((client_state == ClientState::IDLE || client_state == ClientState::INIT) &&
      my_state != state_t::not_connected) {
    schedule_retry_();
    return;
  }
  // The parent only bounds DISCONNECTING (10s). If it never got there, or lost the
  // CLOSE_EVT, force the link closed and start a fresh attempt instead of waiting
  // forever in wait_disconnected.
  if (my_state == state_t::wait_disconnected && now - state_started > DISCONNECT_TIMEOUT_MS) {
    bool stalled = force_idle_if_unopened_();
    if (!stalled && ble_client_->state() != ClientState::IDLE && ble_client_->state() != ClientState::INIT) {
      ESP_LOGW(TAG, "Disconnect did not complete; forcing the link closed");
      ble_client_->unconditional_disconnect();
      stalled = true;
    }
    if (stalled && ++stuck_cycles_ >= MAX_STUCK_CYCLES && esp32_ble::global_ble != nullptr) {
      stuck_cycles_ = 0;
      // Cycling the stack stops the presence scan and drops proxy links for a few
      // seconds, so do it at most once every BLE_RESTART_MIN_INTERVAL_MS.
      if (now - last_ble_restart_ < BLE_RESTART_MIN_INTERVAL_MS) {
        ESP_LOGW(TAG, "BLE controller stalled again; keeping the stack up for now");
      } else {
        last_ble_restart_ = now;
        ++ble_restart_count_;
        ESP_LOGW(TAG, "BLE controller stalled; restarting the ESPHome BLE stack (#%u)", ble_restart_count_);
        esp32_ble::global_ble->disable();
        ble_restart_pending_ = true;
        ble_restart_started_ = now;
        ble_restart_attempts_ = 0;
      }
    }
    reset_session_();
    set_state(state_t::not_connected);
    return;
  }
  if (transport_failed_ || (write_pending_ && now - write_started_ > 5000)) {
    ESP_LOGW(TAG, "Transport failed; resetting connection");
    disconnect();
    return;
  }
  if (my_state == state_t::not_connected) {
    if (client_state == ClientState::CONNECTING || client_state == ClientState::CONNECTED ||
        client_state == ClientState::ESTABLISHED) {
      set_state(state_t::connecting);
    } else {
      const bool requested = always_connect || operation_requested.update_status;
      ble_client_->set_auto_connect(requested && now - retry_started_ >= retry_delay_);
      return;  // Advertisements + tracker schedule connection attempts.
    }
  }
  if (my_state == state_t::connecting && now - state_started > connection_timeout) {
    ESP_LOGW(TAG, "Connection/discovery timeout");
    force_idle_if_unopened_();
    disconnect();  // Standard client defers close if the controller is still opening.
    return;
  }
  if (my_state == state_t::authenticating) {
    if (subscribed_ && sesame.is_session_active()) {
      connect_tried = 0;
      stuck_cycles_ = 0;
      retry_delay_ = 0;
      set_state(state_t::running);
      publish_connection_state(true);
      ESP_LOGI(TAG, "Authenticated");
      // Touch login need not contain status. Poll once even when update_interval is never.
      if (!sesame.request_status()) transport_failed_ = true;
    } else if (now - state_started > connection_timeout) {
      ESP_LOGW(TAG, "Notification/authentication timeout");
      disconnect();
      return;
    }
  }
  if (status_pending_) {
    status_pending_ = false;
    operation_requested.update_status = false;
    reflect_sesame_status();
  }
  if (my_state == state_t::running) {
    if (!sesame.is_session_active()) {
      disconnect();
      return;
    }
    if (!always_connect && !operation_requested.update_status && sesame_status.has_value() &&
        tx_count_ == 0 && !write_pending_) {
      disconnect();
      return;
    }
  }
  if (my_state == state_t::authenticating || my_state == state_t::running) pump_tx_();
}

void SesameComponent::update() {
  operation_requested.update_status = true;
  if (my_state == state_t::running && !sesame.request_status()) {
    ESP_LOGW(TAG, "Failed to request status");
    transport_failed_ = true;
  }
}

void SesameComponent::publish_connection_state(bool connected) {
  if (connection_sensor && (!connection_sensor->has_state() || connection_sensor->state != connected))
    connection_sensor->publish_state(connected);
}

void SesameComponent::make_unknown() {
  sesame_status.reset();
  reflect_sesame_status();
}

void
SesameComponent::reflect_sesame_status() {
	// Update sensors without publishing state yet, so that callbacks can read the new values before they are published
	if (pct_sensor) {
		pct_sensor->state = sesame_status ? sesame_status->battery_pct() : NAN;
	}
	if (voltage_sensor) {
		voltage_sensor->state = sesame_status ? sesame_status->voltage() : NAN;
	}
	esphome::optional<bool> pre_crit{};
	if (battery_critical_sensor) {
		pre_crit = battery_critical_sensor->get_state_internal();
		if (sesame_status) {
			battery_critical_sensor->set_state_internal(sesame_status->battery_critical());
		} else {
			battery_critical_sensor->set_state_internal({});
		}
	}

	if (feature) {
		feature->reflect_status_changed();
	}

	// Now publish sensor states after all updates are done, so that callbacks only see the new values
	if (pct_sensor) {
		pct_sensor->publish_state(pct_sensor->state);
	}
	if (voltage_sensor) {
		voltage_sensor->publish_state(voltage_sensor->state);
	}
	if (battery_critical_sensor) {
		battery_critical_sensor->set_state_internal(pre_crit);
		if (sesame_status) {
			battery_critical_sensor->publish_state(sesame_status->battery_critical());
		} else {
			battery_critical_sensor->invalidate_state();
		}
	}
}


}  // namespace esphome::sesame_lock
