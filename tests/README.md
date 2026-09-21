# Verification fixtures

These YAML files are not run by CI; they are the configurations used to verify the
standard-BLE port by hand. All addresses, UUIDs and secrets in them are fake.

| Fixture | What it covers |
| --- | --- |
| `sesame5.yaml` | Minimal single SESAME 5 with a lock, connection and battery sensors. |
| `sesame_minimal_no_ble.yaml` | Upgrade path: only `sesame:` is declared. `esp32_ble`, `esp32_ble_tracker` and `esp32_ble_client` must be auto-loaded. |
| `sesame_touch_poll.yaml` | SESAME Touch in polling mode (`always_connect: false` plus `update_interval`). |
| `sesame_touch_no_interval.yaml` | Must stay valid but warn: `always_connect: false` with `update_interval: never` can never connect. |
| `sesame_full_features.yaml` | The option surface the existing configs use: OS3 lock with every `history_*` / `all_history_*` sensor, and an OS3 Bot with a `running_sensor`. |
| `sesame_merged.yaml` | The merged entrance node: 2x SESAME 5 + 1x SESAME Touch, `esp32_ble_tracker` with an iBeacon presence lambda, `bluetooth_proxy` with 2 slots, PSRAM and `esp32_ble.max_connections: 5`. |
| `sesame_merged_arduino.yaml` | The same merged node on `framework: type: arduino`, which the existing SESAME configuration uses. |
| `sesame_ble_scan.yaml` | `sesame_ble:` plus one SESAME 5, so `sesame_ble.cpp` is compiled. |
| `sesame_ble_only.yaml` | `sesame_ble:` alone, the documented address-discovery setup. |
| `reject_nimble_sdkconfig.yaml` | Must fail: `CONFIG_BT_NIMBLE_ENABLED: y` in `esp32.framework.sdkconfig_options`. |
| `reject_too_few_connection_slots.yaml` | Must fail: 5 BLE clients with `esp32_ble.max_connections: 2`. |

## Running

PlatformIO rejects project paths containing whitespace, so copy the repository to a
path without spaces (for example `%TEMP%\sesame-build`) and run ESPHome from there.

```powershell
$dst = Join-Path $env:TEMP 'sesame-build'
New-Item -ItemType Directory -Path $dst -Force | Out-Null
Copy-Item .\components $dst -Recurse -Force
Copy-Item .\tests $dst -Recurse -Force

$esphome = '<repo>\.venv\Scripts\esphome.exe'
& $esphome config  "$dst\tests\fixtures\sesame_merged.yaml"   # expect: Configuration is valid!
& $esphome compile "$dst\tests\fixtures\sesame_merged.yaml"   # expect: SUCCESS + ESP32-S3 image
```

Expected failures:

```text
reject_nimble_sdkconfig.yaml
  Remove CONFIG_BT_NIMBLE_ENABLED and the old NimBLE build options; sesame uses ESPHome standard BLE.

reject_too_few_connection_slots.yaml
  BLE clients require 5 slots; set esp32_ble.max_connections to at least 5.
```

After a successful build, `sdkconfig.<name>` in the build directory must contain
`CONFIG_BT_BLUEDROID_ENABLED=y` and must not contain any `CONFIG_BT_NIMBLE_*` symbol.
That is the check that the ESPHome stack is the only BLE backend in the image.

## Results of the last run

| Fixture | Framework | Result |
| --- | --- | --- |
| `sesame_merged.yaml` | esp-idf 5.5.5 | SUCCESS, RAM 17.4% (56992/327680), Flash 14.1% (1147179/8126464) |
| `sesame_merged.yaml` via `type: git` from the fork | esp-idf 5.5.5 | SUCCESS, RAM 17.2% (56488/327680), Flash 13.9% (1132051/8126464) |
| `sesame_merged_arduino.yaml` | arduino | SUCCESS, RAM 17.6% (57800/327680), Flash 15.1% (1223407/8126464) |
| `sesame_ble_scan.yaml` | esp-idf 5.5.5 | SUCCESS, RAM 18.9% (62044/327680), Flash 13.4% (1088971/8126464) |
| `sesame_ble_only.yaml` | esp-idf 5.5.5 | SUCCESS, RAM 18.4% (60284/327680), Flash 13.0% (1052379/8126464) |
| `sesame_full_features.yaml` | esp-idf 5.5.5 | SUCCESS, RAM 19.4% (63644/327680), Flash 13.4% (1088055/8126464) |

Every image reports `CONFIG_BT_BLUEDROID_ENABLED=y`, `# CONFIG_BT_NIMBLE_ENABLED is not set`,
`CONFIG_BT_GATTC_ENABLE=y`, `CONFIG_MBEDTLS_CMAC_C=y`, and no warnings from the component
under `-Wall -Wextra`.

Every C++ file in the two components is compiled by at least one fixture:
`sesame_component.cpp`, `sesame_ble_client.cpp`, `lock_feature.cpp` and `bot_feature.cpp` by
`sesame_merged.yaml` / `sesame_full_features.yaml`, and `sesame_ble.cpp` by
`sesame_ble_scan.yaml` / `sesame_ble_only.yaml`.

`sesame_ble` builds on its own only because it also selects the framework CMAC: without
`-DUSE_FRAMEWORK_MBEDTLS_CMAC`, `libsesame3bt-core` compiles its own CMAC fallback and
fails on the missing `mbedtls/config.h`.
