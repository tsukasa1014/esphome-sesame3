#pragma once

#include "sesame_protocol.h"
#include "sesame_ble_client.h"
#include <esphome/components/binary_sensor/binary_sensor.h>
#include <esphome/components/sensor/sensor.h>
#include <esphome/core/component.h>
#include <esphome/core/version.h>
#include <array>
#include <string_view>
#include <vector>
#include "feature.h"

namespace esphome {

namespace sesame_lock {

class BinarySensorWithInvalidate : public binary_sensor::BinarySensor {
 public:
	void set_state_internal(esphome::optional<bool> state) {
		if (state.has_value()) {
			this->flags_.has_state = true;
			this->state = *state;
		} else {
			this->flags_.has_state = false;
		}
	}
	esphome::optional<bool> get_state_internal() const {
		return this->flags_.has_state ? esphome::optional<bool>(this->state) : esphome::nullopt;
	}
};

enum class state_t : int8_t { not_connected, connecting, authenticating, running, wait_disconnected };

class SesameLock;
class BotFeature;
class SesameComponent : public PollingComponent, public libsesame3bt::core::SesameBLEBackend {
	friend class SesameLock;
	friend class BotFeature;

 public:
	SesameComponent(const char* id);
	void init(libsesame3bt::Sesame::model_t model,
	          std::string_view pubkey,
	          std::string_view secret,
	          std::string_view btaddr,
	          std::string_view uuid);
	void setup() override;
	void loop() override;
	void set_ble_client(SesameBLEClient* client) {
		ble_client_ = client;
		client->set_owner(this);
	}
	void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t* param);
	bool write_to_tx(const uint8_t* data, size_t size) override;
	void set_battery_pct_sensor(sensor::Sensor* sensor) { pct_sensor = sensor; }
	void set_battery_voltage_sensor(sensor::Sensor* sensor) { voltage_sensor = sensor; }
	void set_connection_sensor(binary_sensor::BinarySensor* sensor) { connection_sensor = sensor; }
	void set_battery_critical_sensor(BinarySensorWithInvalidate* sensor) { battery_critical_sensor = sensor; }
	void set_connect_retry_limit(uint16_t retry_limit) { connect_limit = retry_limit; }
	void set_connection_timeout(uint32_t timeout) { connection_timeout = timeout; }
	void set_feature(Feature* feature) { this->feature = feature; }
	void set_always_connect(bool always) { this->always_connect = always; }
	// Called by SesameBLEClient when the controller rejects a connection attempt
	// synchronously, so the retry backoff still applies.
	void on_connect_attempt_failed() { connect_attempt_failed_ = true; }
	// Called by SesameBLEClient when the parent settled a link with a connection
	// timeout. It stays pending until a real CLOSE_EVT/DISCONNECT_EVT arrives, so the
	// loop can tell "the controller stopped reporting events" from "the peer went away".
	void on_watchdog_disconnect() { watchdog_pending_ = true; }
	virtual float get_setup_priority() const override { return setup_priority::AFTER_WIFI; };
	virtual void update() override;

 private:
	SesameBLEClient* ble_client_ = nullptr;
	SesameProtocol sesame{*this};
	esphome::optional<SesameProtocol::Status> sesame_status;
	uint32_t retry_started_ = 0;
	uint32_t retry_delay_ = 0;
	uint32_t state_started = 0;
	std::string log_tag_string;
	const char* TAG = "";
	sensor::Sensor* pct_sensor = nullptr;
	sensor::Sensor* voltage_sensor = nullptr;
	BinarySensorWithInvalidate* battery_critical_sensor = nullptr;
	Feature* feature = nullptr;
	binary_sensor::BinarySensor* connection_sensor = nullptr;
	state_t my_state = state_t::not_connected;
	uint16_t connect_limit = 0;
	uint16_t connect_tried = 0;
	uint32_t connection_timeout = 10'000;
	bool always_connect = true;
	bool status_pending_ = false;
	bool transport_failed_ = false;
	bool subscribed_ = false;
	bool write_pending_ = false;
	uint32_t write_started_ = 0;
	uint16_t tx_handle_ = 0;
	uint16_t rx_handle_ = 0;
	uint16_t cccd_handle_ = 0;
	struct Fragment {
		std::array<uint8_t, 20> data{};
		uint8_t size = 0;
	};
	static constexpr size_t TX_QUEUE_SIZE = 32;
	std::array<Fragment, TX_QUEUE_SIZE> tx_queue_{};
	size_t tx_head_ = 0;
	size_t tx_count_ = 0;
	// The parent client only guards DISCONNECTING, so bound this side too and fall
	// back to cycling the BLE stack when the controller stops delivering events.
	static constexpr uint32_t DISCONNECT_TIMEOUT_MS = 15'000;
	static constexpr uint32_t BLE_RESTART_RETRY_MS = 5'000;
	static constexpr uint8_t MAX_STUCK_CYCLES = 3;
	static constexpr uint8_t MAX_BLE_RESTART_ATTEMPTS = 6;
	// Cycling the stack stops the presence scan and drops proxy links for a few
	// seconds, so keep a floor between restarts.
	static constexpr uint32_t BLE_RESTART_MIN_INTERVAL_MS = 600'000;
	uint8_t stuck_cycles_ = 0;
	bool ble_restart_pending_ = false;
	uint32_t ble_restart_started_ = 0;
	uint8_t ble_restart_attempts_ = 0;
	uint8_t ble_restart_count_ = 0;
	uint32_t last_ble_restart_ = 0;
	bool connect_attempt_failed_ = false;
	bool watchdog_pending_ = false;
	// Set when a polling cycle ended normally, so the CLOSE that follows keeps the
	// measured values and skips the failure backoff.
	bool polling_complete_ = false;
	union {
		uint8_t value;
		struct {
			bool update_status : 1;
		};
	} operation_requested{};
	static_assert(sizeof(operation_requested.value) == sizeof(operation_requested));

	void set_state(state_t);
	void reflect_sesame_status();
	void publish_connection_state(bool connected);
	void disconnect(bool keep_measurements = false);
	bool force_idle_if_unopened_();
	void note_stalled_link_();
	void reset_session_(bool keep_measurements = false);
	void schedule_retry_();
	void pump_tx_();
};

}  // namespace sesame_lock
}  // namespace esphome
