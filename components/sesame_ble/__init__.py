import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import esp32, esp32_ble_tracker
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32_ble_tracker"]
AUTO_LOAD = ["esp32_ble_tracker"]

sesame_ble_ns = cg.esphome_ns.namespace("sesame_ble")
SesameBleListener = sesame_ble_ns.class_("SesameBleListener", esp32_ble_tracker.ESPBTDeviceListener)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SesameBleListener),
    }
).extend(esp32_ble_tracker.ESP_BLE_DEVICE_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await esp32_ble_tracker.register_ble_device(var, config)
    cg.add_library("libsesame3bt-core", None, "https://github.com/homy-newfs8/libsesame3bt-core#v0.50.0")
    # libsesame3bt-core builds its own CMAC fallback unless the framework CMAC is
    # selected, and that fallback needs mbedtls/config.h, which is not on a
    # PlatformIO library include path. The advertisement scanner does not use CMAC
    # itself, but PlatformIO compiles the whole library.
    cg.add_build_flag("-DUSE_FRAMEWORK_MBEDTLS_CMAC")
    # A default, not a forced value: the user's sdkconfig_options must still win.
    esp32.set_idf_sdkconfig_default("CONFIG_MBEDTLS_CMAC_C", True)
