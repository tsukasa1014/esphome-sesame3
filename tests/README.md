# Verification fixtures

These YAML files are not run by CI; they are the configurations used to verify the
standard-BLE port by hand. All addresses, UUIDs and secrets in them are fake.

| Fixture | What it covers |
| --- | --- |
| `sesame5.yaml` | Minimal single SESAME 5 with a lock, connection and battery sensors. |
| `sesame_merged.yaml` | The merged entrance node: 2x SESAME 5 + 1x SESAME Touch, `esp32_ble_tracker` with an iBeacon presence lambda, `bluetooth_proxy` with 2 slots, PSRAM and `esp32_ble.max_connections: 5`. |
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
