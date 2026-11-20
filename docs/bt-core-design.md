# BT core 設計 (Classic HID host + BLE NUS 共存)

## 構造
```
CYW43 owner = ble_uart の poll_loop スレッド (OBJECT_ON_CORE(0)、budget 0 のバトン)
 └ BT core: cyw43_arch_init → btstack 初期化 → hci_power_control → cyw43_arch_poll ループ
    ├ BLE : NUS GATT (従来のまま。将来 OTA もここの上)
    └ Classic : bt_classic.cpp (新規。hid_host / SDP / SSP / 再接続)  ← owner スレッドの中でだけ動く
車 (classic_hid object) は BTstack を一切触らない。STREAM 2 本で bt_classic と話す。
```
- 新しい owner は作らない。led も ble_uart も車も「owner は 1 本」(ble_uart.cpp:1492, 2117)。車が自前で
  cyw43_arch_init/poll を回していた二重 owner は廃止。
- 新カーネルでは led オブジェクトが cyw43_arch_init を呼ぶ構成は RP2040 の BLE 無しだけ
  (peripherals.cpp:189)。pico2_w は BLE 有り = ble_uart が owner (ble_uart.cpp:1756)。

## イベント配送: STREAM 方式 (コールバック登録は採らない)
根拠
1. ble_uart は既に「BTstack は owner スレッドだけ、外とは STREAM」の規約 ([[no-btstack-from-caller-thread]],
   ble_uart.cpp:1404-1427, 1667-1715)。rx/tx/ota/ch2 が全部この形。同じ型に揃える。
2. 車のオブジェクトの関数を owner スレッドから呼ぶ (コールバック) と、budget-0 のバトンスレッドが
   他オブジェクトのコードを実行することになり、オブジェクト分離 (STREAM_BIND の席は発行元オブジェクトから
   導出, ble_uart.cpp:1380) と衝突する。owner が長く止まれば BLE/CYW43 の poll が遅れる。
3. HID の入力は 125Hz 程度。64B フレームの STREAM (深さ 16〜32) で足り、満杯は最古上書きで足りる
   (車は最新の入力だけが要る)。

ストリーム (固定 72B フレーム `{kind, len, data[70]}`)
- hid_rx (owner=producer, 車=consumer): OPENED / CLOSED(reason) / REPORT(生レポート) / SET_REPORT_RESP
- hid_tx (車=producer, owner=consumer): CONNECT(addr6) / DISCONNECT / SET_REPORT(report_id, payload)
- ble_uart のメソッド追加: GET_HID_RX_STREAM=13, SET_HID_TX_STREAM=14 (既存 ID は不変)
- 機種固有の処理 (Pro コン互換機の初期化 0x03/0x30、十字キー速度) は車側に置く。カーネルは汎用 HID host。

## GAP / セキュリティ方針の統合
- LE (sm_*) と Classic SSP (gap_ssp_*) は別 API で IO capability が衝突しない。
  LE: ble_uart の NC/JW + 施錠のまま不変。Classic: NO_INPUT_NO_OUTPUT (Just Works)。
- Classic は MITM 保護の無い Just Works なので、**接続先を CONNECT で渡された 1 台のアドレスに限定**する:
  incoming 接続は対象以外 decline、SSP 確認 (USER_CONFIRMATION_REQUEST) は対象のみ肯定
  (gap_ssp_set_auto_accept(0) + 自前で応答)。対象未設定の間は何も受けない。
  discoverable は OFF。connectable は ON (保存済みコントローラのボタン押下再接続を受けるため)。
- link key は BTstack の TLV (SDK btstack_cyw43.c の setup_tlv) に任せる。アプリで LINK_KEY_REQUEST に
  否定応答しない。認証失敗 (AUTHENTICATION_FAILURE / PIN_OR_KEY_MISSING) の時だけ対象の鍵を捨てる。
- 既存 BLE の壊れ方の修正: ble_uart.cpp の HCI_EVENT_DISCONNECTION_COMPLETE は無条件に LE の接続状態を
  全消しする。Classic の切断で LE の承認・通知状態が落ち、再広告が走るので、**handle が LE の
  con_handle の時だけ**処理する。
- flash 保存 (link key): core1 が lockout victim であることが必要 (ble_uart.cpp:1506)。車は main.cpp で
  start_secondary_core() を呼ぶ構成を維持。

## 無効時の互換
Classic はコンパイル時スイッチ `SHIZUKU_BT_CLASSIC` (BUILD のフラグ `//modules/pico_sdk_support:bt_classic`)。
未定義なら bt_classic.cpp はリンクされず、ble_uart.cpp の追加分はすべて #if で消える。btstack_config も別。

## コントローラの登録し直し (BOOTSEL 3 秒長押し) — 仮実装、実機未検証
目的: 別のコントローラへ差し替えるとき、車の再書込みなしで bond を消して新しい相手を探して繋ぐ。
コントローラ側のシンクボタンは従来どおり必要 (BOOTSEL では代替できない)。電源投入時の BOOTSEL は
USB ブートローダになるので、起動後の長押しだけを使う。

### フレーム (固定 72B `{kind, len, data[70]}`、既存の hid_rx / hid_tx にそのまま載せる)
hid_tx (車 → カーネル) 追加: 
- TX_FORGET=4: len=0 全消去 / len>=6 その相手だけ。接続中は切断し、全消去かその相手が target なら target 解除。
- TX_SCAN_START=5: data[0]=秒 (1.28 秒単位に切上げ、最大 61 秒)。接続中なら切断。
- TX_SCAN_STOP=6 / TX_LIST_BONDS=7: 引数なし。
hid_rx (カーネル → 車) 追加:
- RX_SCAN_FOUND=5: addr6 + CoD3 (上位バイト先)。重複は排除しない。
- RX_SCAN_DONE=6: data[0]= 0 時間切れ / 1 止めた (TX_SCAN_STOP, スキャン中の TX_CONNECT) / 2 開始失敗 (data[1]=status)。
- RX_BOND=7: addr6 / RX_BOND_END=8: data[0]=件数 (RX_BOND は最大 8 件)。

### カーネル bt_classic の状態遷移
- 通常 → SCAN (TX_SCAN_START): 接続を切り、再試行タイマーを解除、gap_inquiry_start。SCAN 中は connect_now() が
  gate され、schedule_retry() は何もせず、incoming も decline。
- SCAN → 通常: GAP_EVENT_INQUIRY_COMPLETE (時間切れ/停止) で RX_SCAN_DONE を出し、target があれば connect_now()。
- SCAN 中の TX_CONNECT: target を更新して inquiry を止め、COMPLETE を待って新 target へ接続 (page と inquiry は並走させない)。
- TX_FORGET(全) は SCAN を止めない (forget → scan の順に送る)。target 解除中は再接続しない。
- TX_LIST_BONDS は HCI が WORKING になる前なら保留し、WORKING のイベントで返す。
- BTstack 呼出しはすべて既存どおり owner スレッド (on_command / packet_handler) 内。

### 車 (classic_hid.cpp)
STARTUP: TX_LIST_BONDS → RX_BOND_END で bond (既定アドレスがあれば優先、なければ最初の 1 件) か REMOTE_BD_ADDR へ TX_CONNECT
(15 秒応答がなければ既定へ)。NORMAL: BOOTSEL を 100 ms 周期で読み、3 秒で TX_FORGET → TX_SCAN_START(30)。
SCAN: ゲームパッド/ジョイスティック CoD (major 0x05, minor 1/2) の最初の相手へ TX_CONNECT。見つからず SCAN_DONE (か 40 秒超過) なら既定へ。
LED は中央 4 灯 (set_central): 琥珀の点滅 = 探索中、白 = 接続中、青 = HID 接続 (既存)。
BOOTSEL は gpio object (gpio_method 4)。ビルドは `--@shizuku//configs:bootsel_support=enabled` が必要で、
車の main.cpp が register_gpio() で gpio object だけ登録する。
