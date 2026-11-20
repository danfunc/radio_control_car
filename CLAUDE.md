# CLAUDE.md — radio_control_car (Pico 2 W / Shizuku)

このファイルは AI エージェント向けの作業メモ。人間向けの短い案内は README.md。
対象の人間: Pico と Git は触ったことがあるが、Shizuku (自作カーネル) と BTstack は知らない後輩。

## 0. 最初に読むこと: 実機未検証の範囲

次は **コードは書いてありビルドも通したが、実機で一度も動かしていない**。
動作を断定する文を書かない。「実機で確認が要る」と書き、結果はログで示す。

- BOOTSEL 3 秒長押しによるコントローラ登録し直し (`firmware_bazel/classic_hid.cpp`、カーネル側 `bt_classic.cpp`)
- 起動時の bond 列挙 (`TX_LIST_BONDS`) からの接続先決定
- この車への BLE OTA の組み込み (`firmware_bazel/main.cpp`、`@xno//objects/ota:ota`)

実機で確かめ済みの前提は無いものとして扱う。確かめたら、この節の該当行を更新する。

## 1. 構成

```
車 (firmware_bazel/)  ──STREAM──>  BT core = Shizuku の ble_uart (CYW43 の唯一の owner、core 0)
 ├ car_objects.cpp   io object (モーター PWM / WS2812 / ラインセンサ)       ├ BLE: NUS + OTA の GATT
 ├ classic_hid.cpp   Switch Pro 互換コンの入力 → 車の指令、BOOTSEL 監視     ├ Classic: bt_classic.cpp (HID host)
 └ main.cpp          オブジェクト登録と BT core / OTA の結線               └ OTA: xno::ota (@xno) → xno_ota_media → flash
```

- 車のコードは **BTstack に一切触らない**。BT core との会話は `hid_tx` / `hid_rx` の STREAM だけ
  (`classic_hid.cpp` 冒頭コメント。設計メモは `docs/bt-core-design.md`)。
- 依存 (`MODULE.bazel`): `shizuku` と `xno` は `git_override` (remote + commit 固定) なので、clone だけでビルドできる。
  Shizuku は `exp/bt-core-classic-20261120` ブランチのコミットを指している。
  手元で Shizuku/XNO を直しながら試すときだけ `.bazelrc.user` (gitignore 済み) に書く:
  ```
  common --override_module=shizuku=../Shizuku
  common --override_module=xno=/path/to/XNO
  ```
- OTA 送信ツールは Shizuku リポジトリの `tools/ble_ota_send.py` (CDC 承認 `--cdc-port` 付き)。
  XNO の `tools/ble_ota_send.py` は古い版で CDC 承認が無いので使わない。

## 2. 前提環境

- `bazelisk`、`picotool` (macOS は `/opt/picotool/bin/picotool`。`build:macos --define=picotool=host`)、
  Python 3 + `bleak` (`pip install bleak`)。OTA ツールは `termios` を使うので macOS / Linux のみ。
- Pico 2 W (RP2350)。ビルドは必ず `--config=rp2350_pico2_w` を付ける。付けないと別の構成になり壊れる。
- ESP32 系のビルドはこのリポジトリの対象外。

## 3. ビルド

```
bazelisk build --config=rp2350_pico2_w //firmware_bazel:car_uf2   # 初回 USB 書込み用 → bazel-bin/firmware_bazel/car.uf2
bazelisk build --config=rp2350_pico2_w //firmware_bazel:car_bin   # OTA 用の生バイナリ → bazel-bin/firmware_bazel/car.bin
```

- OTA に渡すのは **`car.bin`** (UF2 でも ELF でもない)。
- 像が本体区画 1 MiB を越えるとリンクで落ちる (`firmware_bazel/rp2350_flash_limit.ld`)。落ちたら機能を削る。
  OTA を黙って無効化して通さない。
- `.bazelrc` の `--@shizuku//configs:bootsel_support=enabled` を外すと BOOTSEL 長押しは「BOOTSEL read unavailable」
  のログを出して無効になる (ビルドは通る)。
- bazel が sandbox 等で拒否されたら、推測で迂回せず、実行すべきコマンドを人間に示して止まる。

## 4. 初回の USB 書込み (OTA は 2 回目以降)

1. BOOTSEL ボタンを押したまま USB を挿す (ドライブ `RP2350` が出る)。
2. `picotool load -v -x bazel-bin/firmware_bazel/car.uf2`
   - Pico が複数台つながっているときは **ファイル名の後ろ** に `--ser <シリアル>` を付ける
     (`picotool load -v -x bazel-bin/firmware_bazel/car.uf2 --ser E66...`)。シリアルは `picotool info -d`。
3. **電源投入時に BOOTSEL を押しっぱなしにしない** (USB ブートローダになる)。
   登録し直しの長押しは起動後にだけ使う。

## 5. コントローラ (Switch Pro 互換機) のペアリングと登録し直し

- 起動時、保存済みの相手 (bond) があればその相手へ、無ければ既定アドレス `REMOTE_BD_ADDR`
  (`classic_hid.cpp:20` 付近、98:B6:B4:74:B1:55) へ接続する。
- **登録し直し**: 起動後に BOOTSEL を **3 秒長押し** → 保存済み鍵を全消去し、30 秒間コントローラを探す。
  - コントローラ側のシンクボタン (ペアリング待ち) は **別に自分で押す**。BOOTSEL では代替できない。
  - 最初に見つかったゲームパッドの CoD (major 0x05、minor 1/2) の相手へ接続する。
  - LED (中央 4 灯): 琥珀の点滅 = 探索中 / 白 = 接続中 / 青 = HID 接続済み。見つからなければ既定アドレスへ戻る。
- 同時に 2 台まで (`NVM_NUM_LINK_KEYS 2`、`modules/pico_sdk_support/config_classic/`)。別の機種を試すと古い鍵が押し出される。
- 車は走行中に入力が 0.5 秒途切れたらモーターを止める。登録し直しの開始時も止める。

## 6. PC と BLE ボンドして OTA する

車の BLE は **ペアリング施錠** されている (`ble_uart.cpp:250-290` のコメントが正本)。
LE のボンドは 1 件だけで、ボンド済みなら新規ペアリングは拒否される。ペアリング承認は **USB CDC 経由のみ**
(`PAIR? ... num=NNNNNN` が出たら `Y NNNNNN` を返す)。`--cdc-port` を渡せばツールが自動で返す。

```
# 1) 見つかるか (広告名 "Shizuku UART")
python3 <Shizuku>/tools/ble_ota_send.py scan
# 2a) 初回 (その PC と車がまだボンドしていない): 車を USB で PC に挿し、CDC で承認させながら送る
#     (--cdc-port は macOS なら /dev/cu.usbmodem*)
python3 <Shizuku>/tools/ble_ota_send.py send bazel-bin/firmware_bazel/car.bin --commit \
        --cdc-port /dev/cu.usbmodemXXXX
# 2b) 2 回目以降 (ボンド済みの同じ PC): USB は不要。保存済みの鍵で再暗号化すると認可される
python3 <Shizuku>/tools/ble_ota_send.py send bazel-bin/firmware_bazel/car.bin --commit
```

- USB が要るのは **最初のペアリング承認だけ**。ボンド済みの PC が再接続すると `reencryption complete -> authorized`
  で OTA が許可される (`ble_uart.cpp` の `SM_EVENT_REENCRYPTION_COMPLETE`)。2b が無線だけで通るかは実機未確認。

- `send` の主な引数 (実装 `ble_ota_send.py:913-928` から): `--mode {xnor,xnou}` (既定 xnor)、`--commit`
  (転送成功後に XNOC で flash へ反映して再起動)、`--address` (BLE アドレス。省略時は自動探索)、`--cdc-port`、
  `--cdc-mode {nc,jw}` (既定 nc)、`--no-reconnect`、`--max-rounds` (既定 15)。
- 初めてペアリングするとき (ボンドが無い車) は、PC 側に番号確認が出る可能性がある (OS の挙動は未確認)。
  CDC に出る `num=` と一致するかは人間が見る。AI が承認操作を勝手に増やさない。
- **`--commit` 無しだと staging までで止まる** (`done: N bytes crc=... OK (staged at 0x...)` が出る)。
  commit だけを後から単独で送れるかは未確認。基本は 1 回で `--commit` まで通す。

### 完了の確認

1. ツールのログに `done: ... OK` と、続いて `commit: ... no return` が出る (`ble_ota_send.py:260-291` の判定文字列)。
2. 車が再起動する (USB CDC が一度消えて戻る。BLE が再び "Shizuku UART" で広告する)。
3. CDC ログの `[CAR] build: <日付> <時刻>` が新しいビルドのものになっている。
   (OTA の成功は「ツールが成功と言った」では足りない。起動後のログで新しい像を確認する。)

### 失敗したら

- 転送が途中で切れる: ツールは再接続して続きを送る (`--no-reconnect` で無効)。繰り返すなら `--packet-delay` を伸ばす。
- **commit 中 (flash を書き換えている最中) に電源が落ちた / 起動しない**: A/B 面は無い
  (`Shizuku/docs/design/rp-ota-commit-retry.typ`)。**USB で書き直す** (§4)。これが正規の復旧。
- **BLE から入れなくなった (施錠)** — 調べた結果:
  - 解錠は `SET_PAIRING_LOCK` (method 11) と `FORGET_BONDS` (method 12) を呼ぶことでしかできない
    (`ble_uart.hpp` の `method` 参照)。コメントにある「有線の `nc allow` / `nc forget!`」は **Shizuku の
    シェル (XIAO 構成) の機能** で、**この車には無い** (車のコードは 11 / 12 を呼ばない。CDC で受け付けるのは
    `A` `S` `R` `Y` `N` だけ: `ble_uart.cpp:401-440`)。つまり車単体では BLE の施錠を CDC から外せない。
  - 復旧の候補 (どれも実機未検証): ① `picotool erase -a` で flash を全消去 → §4 で書き直し。ボンドも
    Classic の鍵も消えるので一番確実。② bonding バンクだけ消す (範囲は `flash_map.hpp` の
    `BT_RESERVED_OFFSET` から。Pico 2 W 4 MiB なら末尾から 3 番目の 2 セクタのはずだが、**数値は未確認**なので
    アドレスを断定して打たない)。③ 将来、車に CDC コマンド (`FORGET_BONDS` を呼ぶ) を足す案。
    これは施錠の趣旨を変える設計判断なので、人間の了承なしにやらない。
  - 予防: PC 側 (macOS の「このデバイスを削除」など) で車のボンドを消さない。消すと施錠で入れなくなる。

## 7. 触ってはいけない所

- **OTA 中 (特に commit 後) に電源を切らない・USB を抜かない。** 書込み中の電断は USB 復旧になる。
- **ペアリング施錠を外さない・弱めない。** 施錠は鍵の押し出し (乗っ取りと OTA 喪失) を防ぐ仕組み。
  `ble_uart.cpp` の施錠ロジックを直す前に、必ず人間に確認する。
- `cyw43_arch_init` / BTstack を車のコードから呼ばない。BTstack は単一インスタンスで非スレッドセーフ。
  BT core (ble_uart の owner スレッド) の中でだけ触る。
- Shizuku / XNO の中は、このリポジトリの作業では編集しない (別リポジトリ。変更が要るなら人間に依頼する)。
- `sed` / python のワンライナーでの複数箇所の機械的な一括置換をしない。文脈を見て直す。

## 8. AI の作業規約

- 実機未検証の箇所を断定しない (§0)。「ビルドが通った」「ホストで確認した」「実機で動いた」は別の主張として書き分ける。
- ビルドコマンドは `--config=rp2350_pico2_w` 付き。終了コード 0 だけで機能の検証済みとしない。
- テストや確認を追加したら、実行したコマンドと終了コード、見たログの file:line を報告する。
- 新しいファイルを足したら `firmware_bazel/BUILD.bazel` に登録する (Bazel が見つけない)。
- ログの見方 (CDC に出る。実装の文字列から):
  - `[CAR] build:` 起動した像のビルド時刻。`[CAR] object startup failures=N` が 0 でなければオブジェクト登録の失敗。
  - `[BT_CLASSIC] hid_host_connect rc=0x..` / `HID opened` / `next connect in N ms` / `incoming declined` /
    `scan done reason=N` (0 時間切れ・1 停止・2 開始失敗) / `forgot all link keys` / `auth failed .., dropped link key`
  - `[BLE_UART] PAIR? conn=.. mode=NC num=NNNNNN` (承認待ち) / `pairing DECLINED (locked, ...)` (施錠で拒否) /
    `authorized via CDC` / `ota write dropped (unauthorized ...)` (未認可リンクからの OTA は捨てる)
  - `[OTA] registered ...` / `[OTA] input slot ... <- stream ...` (結線できた印)、`[CAR] register_ota failed` (OTA だけ無効)
  - `[CAR] BOOTSEL read unavailable` (BOOTSEL 機能が無効。フラグか gpio object の問題)、`[CAR] bond ...` / `scan found ...`

## 9. OTA の経路 (調べるときの入口)

BLE の OTA characteristic に書込み → `ble_uart.cpp` (cmd_authorized のときだけ `g_ota_rx` へ push、約 `:815-840`)
→ `main.cpp` が結線した STREAM → `xno::ota` (`@xno//objects/ota/ota.cpp`、チャンク受信 + CRC + staging へ書込み)
→ `xno_ota_media_*` (Shizuku `modules/pico_sdk_support/objects/xno_ota_media.cpp`、flash 消去/書込み/再起動)
→ XNOC の commit で staging から本体 (offset 0) へ上書きコピー → 再起動。
staging は RP2350 で 2 MiB 付近から bonding バンクの手前まで (`flash_map.hpp`)。
