# 検証用 fixture

このYAML群はCIでは実行しない。標準BLE移植を手作業で検証するために使った設定ファイルで、
含まれるアドレス・UUID・secret はすべて偽物。

| Fixture | 何を確認するか |
| --- | --- |
| `sesame5.yaml` | 最小構成。SESAME 5 が1台、lock・connection・battery センサー付き。 |
| `sesame_minimal_no_ble.yaml` | 移行経路。`sesame:` だけを書いた構成で、`esp32_ble` / `esp32_ble_tracker` / `esp32_ble_client` が自動読み込みされること。 |
| `sesame_touch_poll.yaml` | SESAME Touch のポーリング動作（`always_connect: false` と `update_interval`）。 |
| `sesame_touch_no_interval.yaml` | 有効なままだが警告が出ること: `always_connect: false` と `update_interval: never` の組み合わせは一度も接続しない。 |
| `sesame_full_features.yaml` | 既存設定が使うオプション面。全 `history_*` / `all_history_*` センサー付きのOS3ロック、`running_sensor` 付きのOS3 Bot、公開lock APIの3経路（タグなし・文字列タグ＝NaNタグ型・16バイトUUIDタグ＝タグ種別つき）を呼ぶボタン。 |
| `sesame_merged.yaml` | 玄関の統合構成。SESAME 5 ×2 + SESAME Touch ×1、iBeacon Presence のlambda付き `esp32_ble_tracker`、2スロットの `bluetooth_proxy`、PSRAM、`esp32_ble.max_connections: 5`。 |
| `sesame_merged_arduino.yaml` | 既存のSESAME設定が使う `framework: type: arduino` での共存確認用。SESAME 5 ×2 + Touch、iBeacon UUIDの一致だけでRSSIを出す簡易Presence、`bluetooth_proxy` 2スロット。存在判定のタイムアウトやmajor/minor判定など統合版のPresenceロジックは含まない。全オプションの網羅は `sesame_merged.yaml` と `sesame_full_features.yaml` が担当する。 |
| `sesame_ble_scan.yaml` | `sesame_ble:` と SESAME 5 を1台。`sesame_ble.cpp` をコンパイル対象に入れるため。 |
| `sesame_ble_only.yaml` | `sesame_ble:` のみ。ドキュメントに載っているアドレス調査手順そのもの。 |
| `reject_nimble_sdkconfig.yaml` | 失敗すること: `esp32.framework.sdkconfig_options` に `CONFIG_BT_NIMBLE_ENABLED: y`。 |
| `reject_too_few_connection_slots.yaml` | 失敗すること: BLEクライアント5本に対して `esp32_ble.max_connections: 2`。 |

## 実行方法

PlatformIO は空白を含むパスを拒否するので、リポジトリを空白の無いパス
（例: `%TEMP%\sesame-build`）へコピーしてから ESPHome を実行する。

```powershell
$dst = Join-Path $env:TEMP 'sesame-build'
New-Item -ItemType Directory -Path $dst -Force | Out-Null
Copy-Item .\components $dst -Recurse -Force
Copy-Item .\tests $dst -Recurse -Force

$esphome = '<repo>\.venv\Scripts\esphome.exe'
& $esphome config  "$dst\tests\fixtures\sesame_merged.yaml"   # 期待: Configuration is valid!
& $esphome compile "$dst\tests\fixtures\sesame_merged.yaml"   # 期待: SUCCESS + ESP32-S3 image
```

期待するエラー（コンポーネントが出力する実際のメッセージ。英語のまま）:

```text
reject_nimble_sdkconfig.yaml
  Remove CONFIG_BT_NIMBLE_ENABLED and the other CONFIG_BT_NIMBLE_* options; sesame uses the
  ESPHome standard BLE stack (Bluedroid).

reject_too_few_connection_slots.yaml
  BLE components reserve 5 connection slots (bluetooth_proxy, sesame) but
  esp32_ble.max_connections is 2. Set esp32_ble.max_connections to at least 5; every SESAME
  keeps its own slot because it stays connected.
```

ビルド成功後、ビルドディレクトリの `sdkconfig.<name>` に `CONFIG_BT_BLUEDROID_ENABLED=y`、
`CONFIG_BT_GATTC_ENABLE=y`、`CONFIG_MBEDTLS_CMAC_C=y` があり、
`# CONFIG_BT_NIMBLE_ENABLED is not set` になっていること。これが「ESPHome標準BLEだけが
イメージに入っている」ことの確認方法。ESP-IDF は Bluedroid のみの構成でも
`CONFIG_BT_NIMBLE_COEX_*`（`..._TX_RX_TLIM_DIS=y`）を既定値として出力するため、
判定に使うのは `CONFIG_BT_NIMBLE_ENABLED` の行であり、`CONFIG_BT_NIMBLE_` の接頭辞一致ではない。

## 直近の実行結果

以下はすべて ESPHome 2026.9.0 で取得した。数値はビルドごとに数百バイト変動するので、
しきい値ではなく目安として扱うこと。「対象コミット」列が「未記録」の行は、記録を残して
いなかった過去のビルドの値で、現在のコードとは一致しない可能性がある。

| Fixture | Framework | 対象コミット | 結果 |
| --- | --- | --- | --- |
| `sesame_merged.yaml` | esp-idf 5.5.5 | 1065638（2026-09-23） | SUCCESS, RAM 17.4% (57016/327680), Flash 14.1% (1147963/8126464) |
| `sesame_merged.yaml`（forkから `type: git` で取得） | esp-idf 5.5.5 | 未記録 | SUCCESS, RAM 17.2% (56488/327680), Flash 13.9% (1132051/8126464) |
| `sesame_merged_arduino.yaml` | arduino | 未記録 | SUCCESS, RAM 17.6% (57824/327680), Flash 15.1% (1224463/8126464) |
| `sesame_ble_scan.yaml` | esp-idf 5.5.5 | 未記録 | SUCCESS, RAM 18.9% (62044/327680), Flash 13.4% (1088971/8126464) |
| `sesame_ble_only.yaml` | esp-idf 5.5.5 | 未記録 | SUCCESS, RAM 18.4% (60284/327680), Flash 13.0% (1052379/8126464) |
| `sesame_full_features.yaml` | esp-idf 5.5.5 | 1065638（2026-09-23） | SUCCESS, RAM 19.5% (63900/327680), Flash 13.4% (1091767/8126464) |

どのイメージも `CONFIG_BT_BLUEDROID_ENABLED=y`、`# CONFIG_BT_NIMBLE_ENABLED is not set`、
`CONFIG_BT_GATTC_ENABLE=y`、`CONFIG_MBEDTLS_CMAC_C=y` で、`-Wall -Wextra` での
コンポーネント由来の警告は0件。

2つのコンポーネントに含まれる全 `.cpp` は、少なくとも1つのfixtureでコンパイルされる:
`sesame_component.cpp` / `sesame_ble_client.cpp` / `lock_feature.cpp` / `bot_feature.cpp` は
`sesame_merged.yaml` と `sesame_full_features.yaml`、`sesame_ble.cpp` は
`sesame_ble_scan.yaml` と `sesame_ble_only.yaml`。

`sesame_ble` が単独でビルドできるのは、このコンポーネントもフレームワークCMACを選んでいる
ため。`-DUSE_FRAMEWORK_MBEDTLS_CMAC` が無いと `libsesame3bt-core` が自前のCMACフォールバックを
コンパイルし、`mbedtls/config.h` が無いことで失敗する。
