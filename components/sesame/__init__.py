import logging
import string

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import binary_sensor, esp32, esp32_ble, esp32_ble_client, esp32_ble_tracker, lock, sensor, text_sensor
from esphome.const import (
    CONF_ADDRESS,
    CONF_ID,
    CONF_MODEL,
    CONF_TAG,
    CONF_TIMEOUT,
    CONF_UUID,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_EMPTY,
    DEVICE_CLASS_RUNNING,
    DEVICE_CLASS_VOLTAGE,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_NONE,
    UNIT_EMPTY,
    UNIT_PERCENT,
    UNIT_VOLT,
)
from esphome.core import CORE
from esphome.types import ConfigType

_LOGGER = logging.getLogger(__name__)

AUTO_LOAD = ["sensor", "text_sensor", "binary_sensor", "lock", "esp32_ble_tracker", "esp32_ble_client"]
DEPENDENCIES = ["esp32", "sensor", "text_sensor", "binary_sensor"]
CONFLICTS_WITH = ["sesame_server"]
MULTI_CONF = True

lock_ns = cg.esphome_ns.namespace("lock")
LockState_t = lock_ns.enum("LockState", False)
LOCK_STATES = {
    "NONE": LockState_t.LOCK_STATE_NONE,
    "LOCKED": LockState_t.LOCK_STATE_LOCKED,
    "UNLOCKED": LockState_t.LOCK_STATE_UNLOCKED,
    "JAMMED": LockState_t.LOCK_STATE_JAMMED,
    "LOCKING": LockState_t.LOCK_STATE_LOCKING,
    "UNLOCKING": LockState_t.LOCK_STATE_UNLOCKING,
}

sesame_lock_ns = cg.esphome_ns.namespace("sesame_lock")
SesameComponent = sesame_lock_ns.class_("SesameComponent", cg.PollingComponent)
SesameLock = sesame_lock_ns.class_("SesameLock", lock.Lock)
BotFeature = sesame_lock_ns.class_("BotFeature")
BinarySensorWithInvalidate = sesame_lock_ns.class_("BinarySensorWithInvalidate", binary_sensor.BinarySensor)
# Owned by SesameComponent; keeps the ESPHome BLE stack as the only backend.
SesameBLEClient = sesame_lock_ns.class_("SesameBLEClient", esp32_ble_client.BLEClientBase)

CONF_PUBLIC_KEY = "public_key"
CONF_SECRET = "secret"
CONF_BATTERY_PCT = "battery_pct"
CONF_BATTERY_VOLTAGE = "battery_voltage"
CONF_BATTERY_CRITICAL = "battery_critical"
CONF_HISTORY_TAG_S = "tag"
CONF_HISTORY_TYPE_S = "type"
CONF_HISTORY_TAG_TYPE_S = "tag_type"
CONF_HISTORY_SCALED_VOLTAGE_S = "scaled_voltage"
CONF_HISTORY_BATTERY_PCT_S = "battery_pct"
CONF_HISTORY_SCALED_VOLTAGE2_S = "scaled_voltage2"
CONF_HISTORY_BATTERY_PCT2_S = "battery_pct2"
CONF_HISTORY_EXTRA_S = "extra"
CONF_TRIGGER_TYPE_S = "trigger_type"
CONF_CONNECT_RETRY_LIMIT = "connect_retry_limit"
CONF_UNKNOWN_STATE_ALTERNATIVE = "unknown_state_alternative"
CONF_CONNECTION_SENSOR = "connection_sensor"
CONF_UNKNOWN_STATE_TIMEOUT = "unknown_state_timeout"
CONF_LOCK = "lock"
CONF_BOT = "bot"
CONF_RUNNING_SENSOR = "running_sensor"
CONF_ALWAYS_CONNECT = "always_connect"
CONF_FAST_NOTIFY = "fast_notify"
CONF_INTERNAL_BLE_CLIENT_ID = "internal_ble_client_id"

SesameModel_t = cg.global_ns.enum("libsesame3bt::Sesame::model_t", True)
SESAME_MODELS = {
    "sesame_3": SesameModel_t.sesame_3,
    "sesame_bot": SesameModel_t.sesame_bot,
    "sesame_bike": SesameModel_t.sesame_bike,
    "sesame_cycle": SesameModel_t.sesame_bike,
    "sesame_4": SesameModel_t.sesame_4,
    "sesame_5": SesameModel_t.sesame_5,
    "sesame_bike_2": SesameModel_t.sesame_bike_2,
    "sesame_5_pro": SesameModel_t.sesame_5_pro,
    "sesame_touch_pro": SesameModel_t.sesame_touch_pro,
    "sesame_touch": SesameModel_t.sesame_touch,
    "remote": SesameModel_t.remote,
    "sesame_5_us": SesameModel_t.sesame_5_us,
    "sesame_bot_2": SesameModel_t.sesame_bot_2,
    "sesame_face_pro": SesameModel_t.sesame_face_pro,
    "sesame_face": SesameModel_t.sesame_face,
    "sesame_6": SesameModel_t.sesame_6,
    "sesame_6_pro": SesameModel_t.sesame_6_pro,
    "sesame_face_pro_ai": SesameModel_t.sesame_face_pro_ai,
    "sesame_face_ai": SesameModel_t.sesame_face_ai,
    "open_sensor_2": SesameModel_t.open_sensor_2,
    "sesame_touch_2": SesameModel_t.sesame_touch_2,
    "sesame_touch_2_pro": SesameModel_t.sesame_touch_2_pro,
    "sesame_face_2": SesameModel_t.sesame_face_2,
    "sesame_face_2_pro": SesameModel_t.sesame_face_2_pro,
    "sesame_face_2_ai": SesameModel_t.sesame_face_2_ai,
    "sesame_face_2_pro_ai": SesameModel_t.sesame_face_2_pro_ai,
    "sesame_bot_3": SesameModel_t.sesame_bot_3,
}


def is_os3_model(model):
    return model not in ("sesame_3", "sesame_bot", "sesame_bike", "sesame_cycle", "sesame_4")


def is_lockable_model(model):
    return model in (
        "sesame_3",
        "sesame_bot",
        "sesame_bike",
        "sesame_cycle",
        "sesame_4",
        "sesame_5",
        "sesame_bike_2",
        "sesame_5_pro",
        "sesame_5_us",
        "sesame_6",
        "sesame_6_pro",
    )


def validate_standard_ble(config):
    """Reject a leftover NimBLE stack and an undersized BLE connection pool.

    Both are much easier to diagnose here than as a door lock that silently fails
    to connect at runtime.
    """
    full = fv.full_config.get()
    options = full.get("esp32", {}).get("framework", {}).get("sdkconfig_options", {})
    if str(options.get("CONFIG_BT_NIMBLE_ENABLED", "n")).lower() in ("y", "true", "1"):
        raise cv.Invalid(
            "Remove CONFIG_BT_NIMBLE_ENABLED and the other CONFIG_BT_NIMBLE_* options; "
            "sesame uses the ESPHome standard BLE stack (Bluedroid)"
        )
    leftovers = sorted(key for key in options if key.startswith("CONFIG_BT_NIMBLE_"))
    if leftovers:
        _LOGGER.warning(
            "sesame no longer uses NimBLE; these sdkconfig options have no effect and can be removed: %s",
            ", ".join(leftovers),
        )
    used = CORE.data.get(esp32_ble.KEY_ESP32_BLE, {}).get(esp32_ble.KEY_USED_CONNECTION_SLOTS, [])
    maximum = full.get("esp32_ble", {}).get("max_connections", esp32_ble.DEFAULT_MAX_CONNECTIONS)
    if len(used) > maximum:
        hint = ""
        if "max_connections" in full.get("esp32_ble_tracker", {}):
            # esp32_ble only warns about the deprecated location and keeps using its
            # own default, so that value cannot be honoured here either.
            hint = (
                " Note: max_connections under esp32_ble_tracker is deprecated and ignored;"
                " move it to esp32_ble."
            )
        raise cv.Invalid(
            f"BLE components reserve {len(used)} connection slots "
            f"({', '.join(sorted(set(used)))}) but esp32_ble.max_connections is {maximum}. "
            f"Set esp32_ble.max_connections to at least {len(used)}; every SESAME keeps its own "
            f"slot because it stays connected.{hint}"
        )


FINAL_VALIDATE_SCHEMA = validate_standard_ble


def is_hex_string(str, valid_len):
    return len(str) == valid_len and all(c in string.hexdigits for c in str)


def valid_hexstring(key, valid_len):
    def func(str):
        if is_hex_string(str, valid_len):
            return str
        else:
            raise cv.Invalid(f"'{key}' must be a {valid_len} bytes hex string")

    return func


def validate_pubkey(config: ConfigType) -> ConfigType:
    if not is_os3_model(config[CONF_MODEL]):
        if not config[CONF_PUBLIC_KEY]:
            raise cv.RequiredFieldInvalid("'public_key' is required for SESAME 3 / SESAME 4 / SESAME bot / SESAME Bike")
        valid_hexstring(CONF_PUBLIC_KEY, 128)(config[CONF_PUBLIC_KEY])
    return config


def validate_lockable(config: ConfigType) -> ConfigType:
    if not is_lockable_model(config[CONF_MODEL]):
        if CONF_LOCK in config:
            raise cv.Invalid(f"Cannot define 'lock' for {config[CONF_MODEL]}")
    return config


def validate_always_connect(config: ConfigType) -> ConfigType:
    if not config[CONF_ALWAYS_CONNECT]:
        if CONF_LOCK in config or CONF_BOT in config:
            raise cv.Invalid("When using `lock` or `bot`, `always_connect` must be True")
    return config


def validate_bot_features(config: ConfigType) -> ConfigType:
    if CONF_LOCK in config and CONF_BOT in config:
        raise cv.Invalid("Cannot define both `lock` and `bot` on one Bot device")
    if CONF_BOT in config and config[CONF_MODEL] not in ("sesame_bot", "sesame_bot_2", "sesame_bot_3"):
        raise cv.Invalid("`bot` can be defined in Bot device")
    return config


def validate_address(config: ConfigType) -> ConfigType:
    model = config[CONF_MODEL]
    if is_os3_model(model):
        if CONF_UUID not in config and CONF_ADDRESS not in config:
            raise cv.RequiredFieldInvalid(f"Either 'uuid' or 'address' is required for {model}")
    else:
        if CONF_ADDRESS not in config:
            raise cv.RequiredFieldInvalid(f"'address' is required for {model}")
    return config


CONF_HISTORY_PREFIXES = [
    "history_",
    "all_history_",
]


def validate_deprecation(config: ConfigType) -> ConfigType:
    if CONF_UNKNOWN_STATE_ALTERNATIVE in config:
        _LOGGER.warning(
            f"The option '{CONF_UNKNOWN_STATE_ALTERNATIVE}' is deprecated."
            " As of Home Assistant 2025.10.0, The `unknown` state is properly treated as `unknown`."
        )
    for prefix in CONF_HISTORY_PREFIXES:
        if prefix + CONF_TRIGGER_TYPE_S in config:
            if prefix + CONF_HISTORY_TAG_TYPE_S in config:
                raise cv.Invalid(
                    f"Cannot define both '{prefix + CONF_TRIGGER_TYPE_S}' and '{prefix + CONF_HISTORY_TAG_TYPE_S}'. Please use only '{prefix + CONF_HISTORY_TAG_TYPE_S}'."
                )
            config[prefix + CONF_HISTORY_TAG_TYPE_S] = config.pop(prefix + CONF_TRIGGER_TYPE_S)
            _LOGGER.warning(
                f"The option '{prefix + CONF_TRIGGER_TYPE_S}' is deprecated. Please use '{prefix + CONF_HISTORY_TAG_TYPE_S}' instead. `{prefix + CONF_TRIGGER_TYPE_S}` will be removed in future releases."
            )
    return config


cv.All(cv.version_number, cv.validate_esphome_version)("2026.9.0")


def lock_history_schema(prefix) -> dict:
    return {
        cv.Optional(prefix + CONF_HISTORY_TAG_S): text_sensor.text_sensor_schema(),
        cv.Optional(prefix + CONF_HISTORY_TYPE_S): sensor.sensor_schema(
            unit_of_measurement=UNIT_EMPTY,
            device_class=DEVICE_CLASS_EMPTY,
            state_class=STATE_CLASS_NONE,
            accuracy_decimals=0,
        ),
        cv.Optional(prefix + CONF_TRIGGER_TYPE_S): sensor.sensor_schema(
            unit_of_measurement=UNIT_EMPTY,
            device_class=DEVICE_CLASS_EMPTY,
            state_class=STATE_CLASS_NONE,
            accuracy_decimals=0,
        ),
        cv.Optional(prefix + CONF_HISTORY_TAG_TYPE_S): sensor.sensor_schema(
            unit_of_measurement=UNIT_EMPTY,
            device_class=DEVICE_CLASS_EMPTY,
            state_class=STATE_CLASS_NONE,
            accuracy_decimals=0,
        ),
        cv.Optional(prefix + CONF_HISTORY_SCALED_VOLTAGE_S): sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=2,
        ),
        cv.Optional(prefix + CONF_HISTORY_BATTERY_PCT_S): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            device_class=DEVICE_CLASS_BATTERY,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=1,
        ),
        cv.Optional(prefix + CONF_HISTORY_SCALED_VOLTAGE2_S): sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=2,
        ),
        cv.Optional(prefix + CONF_HISTORY_BATTERY_PCT2_S): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            device_class=DEVICE_CLASS_BATTERY,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=1,
        ),
        cv.Optional(prefix + CONF_HISTORY_EXTRA_S): text_sensor.text_sensor_schema(),
    }


lock_schema = {
    cv.GenerateID(): cv.declare_id(SesameLock),
    cv.Optional(CONF_TAG, default="ESPHome"): cv.string,
    cv.Optional(CONF_UNKNOWN_STATE_ALTERNATIVE): cv.enum(LOCK_STATES),
    cv.Optional(CONF_UNKNOWN_STATE_TIMEOUT, default="20s"): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_FAST_NOTIFY, default=False): cv.boolean,
}


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SesameComponent),
            cv.GenerateID(CONF_INTERNAL_BLE_CLIENT_ID): cv.declare_id(SesameBLEClient),
            cv.Required(CONF_MODEL): cv.enum(SESAME_MODELS),
            cv.Optional(CONF_PUBLIC_KEY, default=""): cv.string,
            cv.Required(CONF_SECRET): valid_hexstring(CONF_SECRET, 32),
            cv.Optional(CONF_ADDRESS): cv.mac_address,
            cv.Optional(CONF_UUID): cv.uuid,
            cv.Optional(CONF_LOCK): cv.All(
                lock.lock_schema().extend(lock_schema | lock_history_schema("history_") | lock_history_schema("all_history_")),
                validate_deprecation,
            ),
            cv.Optional(CONF_BOT): cv.Schema(
                {
                    cv.GenerateID(): cv.declare_id(BotFeature),
                    cv.Optional(CONF_RUNNING_SENSOR): binary_sensor.binary_sensor_schema(
                        device_class=DEVICE_CLASS_RUNNING,
                    ),
                }
            ),
            cv.Optional(CONF_BATTERY_PCT): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                device_class=DEVICE_CLASS_BATTERY,
                state_class=STATE_CLASS_MEASUREMENT,
                accuracy_decimals=1,
            ),
            cv.Optional(CONF_BATTERY_VOLTAGE): sensor.sensor_schema(
                unit_of_measurement=UNIT_VOLT,
                device_class=DEVICE_CLASS_VOLTAGE,
                state_class=STATE_CLASS_MEASUREMENT,
                accuracy_decimals=2,
            ),
            cv.Optional(CONF_BATTERY_CRITICAL): binary_sensor.binary_sensor_schema(
                class_=BinarySensorWithInvalidate,
                device_class=DEVICE_CLASS_BATTERY,
            ),
            cv.Optional(CONF_CONNECT_RETRY_LIMIT): cv.int_range(min=0, max=65535),
            cv.Optional(CONF_CONNECTION_SENSOR): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_CONNECTIVITY,
            ),
            cv.Optional(CONF_TIMEOUT, default="10s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_ALWAYS_CONNECT, default=True): cv.boolean,
        }
    ).extend(cv.polling_component_schema("never")).extend(esp32_ble_tracker.ESP_BLE_DEVICE_SCHEMA),
    validate_address,
    validate_pubkey,
    validate_lockable,
    validate_always_connect,
    validate_bot_features,
    esp32_ble.consume_connection_slots(1, "sesame"),
)


async def add_history_codes(lock_obj, config, prefix):
    if prefix + CONF_HISTORY_TAG_S in config:
        s = await text_sensor.new_text_sensor(config[prefix + CONF_HISTORY_TAG_S])
        cg.add(getattr(lock_obj, "set_" + prefix + CONF_HISTORY_TAG_S + "_sensor")(s))
    if prefix + CONF_HISTORY_TYPE_S in config:
        s = await sensor.new_sensor(config[prefix + CONF_HISTORY_TYPE_S])
        cg.add(getattr(lock_obj, "set_" + prefix + CONF_HISTORY_TYPE_S + "_sensor")(s))
    if prefix + CONF_HISTORY_TAG_TYPE_S in config:
        s = await sensor.new_sensor(config[prefix + CONF_HISTORY_TAG_TYPE_S])
        cg.add(getattr(lock_obj, "set_" + prefix + CONF_HISTORY_TAG_TYPE_S + "_sensor")(s))
    if prefix + CONF_HISTORY_SCALED_VOLTAGE_S in config:
        s = await sensor.new_sensor(config[prefix + CONF_HISTORY_SCALED_VOLTAGE_S])
        cg.add(getattr(lock_obj, "set_" + prefix + CONF_HISTORY_SCALED_VOLTAGE_S + "_sensor")(s))
    if prefix + CONF_HISTORY_BATTERY_PCT_S in config:
        s = await sensor.new_sensor(config[prefix + CONF_HISTORY_BATTERY_PCT_S])
        cg.add(getattr(lock_obj, "set_" + prefix + CONF_HISTORY_BATTERY_PCT_S + "_sensor")(s))
    if prefix + CONF_HISTORY_SCALED_VOLTAGE2_S in config:
        s = await sensor.new_sensor(config[prefix + CONF_HISTORY_SCALED_VOLTAGE2_S])
        cg.add(getattr(lock_obj, "set_" + prefix + CONF_HISTORY_SCALED_VOLTAGE2_S + "_sensor")(s))
    if prefix + CONF_HISTORY_BATTERY_PCT2_S in config:
        s = await sensor.new_sensor(config[prefix + CONF_HISTORY_BATTERY_PCT2_S])
        cg.add(getattr(lock_obj, "set_" + prefix + CONF_HISTORY_BATTERY_PCT2_S + "_sensor")(s))
    if prefix + CONF_HISTORY_EXTRA_S in config:
        s = await text_sensor.new_text_sensor(config[prefix + CONF_HISTORY_EXTRA_S])
        cg.add(getattr(lock_obj, "set_" + prefix + CONF_HISTORY_EXTRA_S + "_sensor")(s))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID], str(config[CONF_ID]))
    await cg.register_component(var, config)
    client = cg.new_Pvariable(config[CONF_INTERNAL_BLE_CLIENT_ID])
    await cg.register_component(client, {CONF_ID: config[CONF_INTERNAL_BLE_CLIENT_ID]})
    await esp32_ble_tracker.register_client(client, config)
    cg.add(client.set_auto_connect(False))
    cg.add(var.set_ble_client(client))
    cg.add_define("USE_ESP32_BLE_UUID")
    esp32_ble.register_bt_logger(esp32_ble.BTLoggers.GATT, esp32_ble.BTLoggers.SMP)
    if CONF_BATTERY_PCT in config:
        s = await sensor.new_sensor(config[CONF_BATTERY_PCT])
        cg.add(var.set_battery_pct_sensor(s))
    if CONF_BATTERY_VOLTAGE in config:
        s = await sensor.new_sensor(config[CONF_BATTERY_VOLTAGE])
        cg.add(var.set_battery_voltage_sensor(s))
    if CONF_CONNECTION_SENSOR in config:
        s = await binary_sensor.new_binary_sensor(config[CONF_CONNECTION_SENSOR])
        cg.add(var.set_connection_sensor(s))
    if CONF_BATTERY_CRITICAL in config:
        s = await binary_sensor.new_binary_sensor(config[CONF_BATTERY_CRITICAL])
        cg.add(var.set_battery_critical_sensor(s))
    if CONF_CONNECT_RETRY_LIMIT in config:
        cg.add(var.set_connect_retry_limit(config[CONF_CONNECT_RETRY_LIMIT]))
    if CONF_TIMEOUT in config:
        cg.add(var.set_connection_timeout(config[CONF_TIMEOUT].total_milliseconds))
    if CONF_ALWAYS_CONNECT in config:
        cg.add(var.set_always_connect(config[CONF_ALWAYS_CONNECT]))

    if CONF_LOCK in config:
        lconfig = config[CONF_LOCK]
        lck = cg.new_Pvariable(lconfig[CONF_ID], var, config[CONF_MODEL], lconfig[CONF_TAG])
        await lock.register_lock(lck, config[CONF_LOCK])
        for prefix in CONF_HISTORY_PREFIXES:
            await add_history_codes(lck, lconfig, prefix)
        if CONF_UNKNOWN_STATE_ALTERNATIVE in lconfig:
            cg.add(lck.set_unknown_state_alternative(lconfig[CONF_UNKNOWN_STATE_ALTERNATIVE]))
        if CONF_UNKNOWN_STATE_TIMEOUT in lconfig:
            cg.add(lck.set_unknown_state_timeout(lconfig[CONF_UNKNOWN_STATE_TIMEOUT].total_milliseconds))
        if CONF_FAST_NOTIFY in lconfig:
            cg.add(lck.set_fast_notify(lconfig[CONF_FAST_NOTIFY]))
        cg.add(var.set_feature(lck))
        cg.add(lck.init())
    if CONF_BOT in config:
        bconfig = config[CONF_BOT]
        bot = cg.new_Pvariable(bconfig[CONF_ID], var, config[CONF_MODEL])
        if CONF_RUNNING_SENSOR in bconfig:
            s = await binary_sensor.new_binary_sensor(bconfig[CONF_RUNNING_SENSOR])
            cg.add(bot.set_running_sensor(s))
        cg.add(var.set_feature(bot))
        cg.add(bot.init())
    address = str(config[CONF_ADDRESS]) if CONF_ADDRESS in config else ""
    uuid = str(config[CONF_UUID]) if CONF_UUID in config else ""
    cg.add(var.init(config[CONF_MODEL], config[CONF_PUBLIC_KEY], config[CONF_SECRET], address, uuid))

    cg.add_library("libsesame3bt-core", None, "https://github.com/homy-newfs8/libsesame3bt-core#v0.50.0")
    cg.add_build_flag("-DUSE_FRAMEWORK_MBEDTLS_CMAC")
    esp32.add_idf_sdkconfig_option("CONFIG_MBEDTLS_CMAC_C", True)
