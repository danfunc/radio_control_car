// Switch Pro コン (互換機含む) の入力 → 車の指令。★BTstack には一切触らない。
// Classic HID host は Shizuku の BT core (ble_uart の owner スレッド上の bt_classic) が
// 持ち、ここは hid_rx / hid_tx の STREAM だけで話す (設計: bt-core-port/DESIGN.md)。
#include "car_objects.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include "shizuku/kernel.hpp"
#include "shizuku/object_api.hpp"
#include "shizuku/objects/ble_uart.hpp"
#include "shizuku/objects/bt_classic.hpp"
#include "shizuku/objects/peripherals.hpp" // gpio_method::BOOTSEL_READ_PRESSED
#include "shizuku/stream.hpp"

namespace car {
namespace {
using shizuku::objects::bt_classic::hid_frame;
namespace bc = shizuku::objects::bt_classic;

#ifndef REMOTE_BD_ADDR
#define REMOTE_BD_ADDR 0x98, 0xB6, 0xB4, 0x74, 0xB1, 0x55
#endif
constexpr uint8_t kRemoteAddr[6] = {REMOTE_BD_ADDR};

shizuku::stream::handle<hid_frame> g_tx; // 車 -> BT core
shizuku::stream::handle<hid_frame> g_rx; // BT core -> 車
uintptr_t g_tx_id = 0;

enum InitState { INIT_IDLE, INIT_SET_MODE, INIT_SET_LED, INIT_DONE };
// 初期化の各段 (0x03 入力モード → 0x30 LED) は ACK/NAK か、この時間の無応答で次へ進む。
// 互換機は応答しない・NAK を返すことがあるので止まらないようにする。振動有効化 (0x48)
// は振動なしの機体なので送らない。
constexpr uint64_t kInitStageTimeoutUs = 300000;
uint64_t init_stage_us = 0;
InitState init_state = INIT_IDLE;
bool init_started = false;
bool hid_open = false;
uint8_t output_counter = 0;

// 速度レベル。既定は最大速度の 1/2。十字キー上で一段上げ、下で一段下げ。
const double kSpeedScale[] = {0.5, 0.75, 1.0};
const char *const kSpeedName[] = {"1/2", "3/4", "1"};
constexpr int kSpeedLevels = 3;
int speed_level = 0;
bool dpad_up_prev = false;
bool dpad_down_prev = false;

// 入力が途切れたら車体を止める (切断イベントが来ない無線断でも走り続けない)。
constexpr uint64_t kReportTimeoutUs = 500000;
uint64_t last_report_us = 0;
bool motors_active = false;
uint32_t motor_log_divider = 0;

uint64_t now_us() { return shizuku::KERNEL::BOARD::time_us(); }
uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint32_t)r << 8) | ((uint32_t)g << 16) | (uint32_t)b;
}

void stop_motors() {
  for (uintptr_t p = 0; p < 4; ++p)
    call(id::io, (uintptr_t)io_method::set_motor, motor_arg(p, 1000));
  motors_active = false;
}

void send_frame(uint8_t kind, const uint8_t *data, uint8_t len) {
  hid_frame f;
  memset(&f, 0, sizeof(f));
  f.kind = kind;
  f.len = len;
  if (len != 0) memcpy(f.data, data, len);
  g_tx.push(f);
}

// Pro コンのサブコマンド (OUTPUT report 0x01):
// [0x01][counter][rumble x8 (neutral)][subcommand][args...]。report id は
// bt_classic の TX_SET_REPORT の data[0]。
void send_subcommand(uint8_t subcommand, const uint8_t *args, uint8_t arg_len) {
  static const uint8_t neutral_rumble[8] = {0x00, 0x01, 0x40, 0x40,
                                            0x00, 0x01, 0x40, 0x40};
  uint8_t buf[1 + 1 + 8 + 1 + 8];
  if (arg_len > 8) arg_len = 8;
  uint8_t n = 0;
  buf[n++] = 0x01;
  buf[n++] = output_counter++ & 0x0F;
  memcpy(&buf[n], neutral_rumble, 8);
  n += 8;
  buf[n++] = subcommand;
  if (arg_len != 0) { memcpy(&buf[n], args, arg_len); n += arg_len; }
  send_frame(bc::TX_SET_REPORT, buf, n);
}

void init_send_led(const char *why) {
  std::printf("[CAR] init: 0x03 stage end (%s) -> LED player 1 (0x30 0x01)\n", why);
  init_state = INIT_SET_LED;
  init_stage_us = now_us();
  const uint8_t arg = 0x01; // bit0 のみ = プレイヤー 1
  send_subcommand(0x30, &arg, 1);
}
void init_finish(const char *why) {
  std::printf("[CAR] init: LED stage end (%s) -> init complete\n", why);
  init_state = INIT_DONE;
}
void init_poll() {
  if (init_state != INIT_SET_MODE && init_state != INIT_SET_LED) return;
  if (now_us() - init_stage_us <= kInitStageTimeoutUs) return;
  if (init_state == INIT_SET_MODE) init_send_led("timeout 300ms");
  else init_finish("timeout 300ms");
}

// 十字キー上下の押した瞬間だけ速度レベルを変える。ビット位置は推定:
//  - 0x30/0x21 フルモード: report[6] が左側ボタン (down=0x01, up=0x02)。
//  - 0x3F 簡易モード: report[4] が hat (0=上, 4=下 ...)。
void handle_dpad(const uint8_t *report, uint16_t len, uint8_t report_id) {
  bool up, down;
  if (report_id == 0x3F) {
    const uint8_t hat = report[4];
    up = (hat == 0 || hat == 1 || hat == 7);
    down = (hat == 3 || hat == 4 || hat == 5);
  } else {
    up = (report[6] & 0x02) != 0;
    down = (report[6] & 0x01) != 0;
  }
  if (up == dpad_up_prev && down == dpad_down_prev) return;
  std::printf("[CAR] dpad: id=%02x report[4]=%02x report[6]=%02x up=%d down=%d len=%u\n",
              report_id, report[4], report[6], up, down, (unsigned)len);
  if (up && !dpad_up_prev && speed_level < kSpeedLevels - 1) {
    speed_level++;
    std::printf("[CAR] speed level up -> %s\n", kSpeedName[speed_level]);
  }
  if (down && !dpad_down_prev && speed_level > 0) {
    speed_level--;
    std::printf("[CAR] speed level down -> %s\n", kSpeedName[speed_level]);
  }
  dpad_up_prev = up;
  dpad_down_prev = down;
}

void on_report(const uint8_t *report, uint16_t len) {
  if (len < 13 || report[0] != 0xA1 ||
      !(report[1] == 0x3F || report[1] == 0x21 || report[1] == 0x30))
    return;
  const uint8_t report_id = report[1];
  int32_t lx, ly, rx, ry;
  if (report_id == 0x3F) { // Standard Mode: 16bit -> 12bit
    lx = (report[5] | (report[6] << 8)) >> 4;
    ly = (report[7] | (report[8] << 8)) >> 4;
    rx = (report[9] | (report[10] << 8)) >> 4;
    ry = (report[11] | (report[12] << 8)) >> 4;
  } else { // Full Mode: 12bit packed
    lx = report[7] | ((report[8] & 0xf) << 8);
    ly = ((report[8] & 0xf0) >> 4) | (report[9] << 4);
    rx = report[10] | ((report[11] & 0xf) << 8);
    ry = ((report[11] & 0xf0) >> 4) | (report[12] << 4);
  }
  lx -= 2048; ly -= 2048; rx -= 2048; ry -= 2048;

  double FL = (rx - (ly + ry) + lx) / 2;
  double FR = (-rx - (ly + ry) - lx) / 2;
  double RL = (-rx - (ly + ry) + lx) / 2;
  double RR = (+rx - (ly + ry) - lx) / 2;
  double maxVal = fabs(FL);
  if (fabs(FR) > maxVal) maxVal = fabs(FR);
  if (fabs(RL) > maxVal) maxVal = fabs(RL);
  if (fabs(RR) > maxVal) maxVal = fabs(RR);
  if (maxVal > 1000) {
    FL = FL / maxVal * 1000; FR = FR / maxVal * 1000;
    RL = RL / maxVal * 1000; RR = RR / maxVal * 1000;
  }
  handle_dpad(report, len, report_id);
  const double scale = kSpeedScale[speed_level];
  const double v[4] = {FL * scale, FR * scale, RL * scale, RR * scale};
  for (uintptr_t p = 0; p < 4; ++p)
    call(id::io, (uintptr_t)io_method::set_motor, motor_arg(p, (uintptr_t)(v[p] + 1000)));
  last_report_us = now_us();
  motors_active = true;
  if (++motor_log_divider % 32 == 0)
    std::printf("Motor: FL:%4ld FR:%4ld RL:%4ld RR:%4ld\n", (long)v[0], (long)v[1],
                (long)v[2], (long)v[3]);

  if (report_id == 0x3F) { // 初期状態 (簡易モード) が来たら入力モードを切り替える
    if (!init_started && hid_open) {
      std::printf("[CAR] simple mode (3F) detected, starting init (0x03 0x30)\n");
      init_started = true;
      init_state = INIT_SET_MODE;
      init_stage_us = now_us();
      const uint8_t arg = 0x30; // standard full mode
      send_subcommand(0x03, &arg, 1);
    }
  } else if (report_id == 0x21 && len >= 15) { // サブコマンドへの返事
    const bool is_ack = (report[13] & 0x80) != 0;
    const uint8_t replied = report[14];
    std::printf("[CAR] %s for cmd 0x%02x\n", is_ack ? "ACK" : "NAK", replied);
    // ACK でも NAK でも次の段へ進む (互換機で止まらないように)。
    if (init_state == INIT_SET_MODE && replied == 0x03) init_send_led(is_ack ? "ACK" : "NAK");
    else if (init_state == INIT_SET_LED && replied == 0x30) init_finish(is_ack ? "ACK" : "NAK");
  } else if (report_id == 0x30 && init_state == INIT_DONE) {
    // フルモードのデータ = 準備完了
  }
}

// ---- BOOTSEL による登録し直し / BLE 一回券 --------------------------------
// ★実機未検証の仮実装。IF (bc::TX_FORGET / TX_SCAN_* / TX_LIST_BONDS と RX_SCAN_* / RX_BOND*)
//   は DESIGN.md「コントローラの登録し直し」に合わせてある。
// 状態遷移:
//   STARTUP --RX_BOND_END (か 15 秒待ち切れ)--> NORMAL : bond があればその相手へ、なければ既定へ TX_CONNECT
//   NORMAL  --BOOTSEL 3〜10 秒で離す--> SCAN : モーター停止、TX_FORGET(全) → TX_SCAN_START(30 秒)
//   NORMAL  --BOOTSEL 10 秒--> NORMAL : BLE の期限付き一回券を発行、離しても SCAN しない
//   SCAN    --RX_SCAN_FOUND (ゲームパッド CoD)--> PAIRING : TX_CONNECT(その相手)
//   SCAN    --RX_SCAN_DONE (見つからず)/ 40 秒超過--> NORMAL : 既定アドレスへ TX_CONNECT
//   PAIRING --RX_SCAN_DONE(1) / RX_OPENED--> NORMAL (接続はカーネルが続ける)
// 起動直後の BOOTSEL 押下は USB ブートローダになるので、起動後の長押しだけを見る。
enum RegMode { REG_STARTUP, REG_NORMAL, REG_SCAN, REG_PAIRING };
RegMode reg_mode = REG_STARTUP;
constexpr uint64_t kBootselPollUs = 100000;
constexpr uint64_t kBootselHoldUs = 3000000;
constexpr uint64_t kBootselBleUs = 10000000;
constexpr uint8_t kScanSeconds = 30;
constexpr uint64_t kScanGuardUs = (uint64_t)(kScanSeconds + 10) * 1000000ull;
constexpr uint64_t kBondQueryTimeoutUs = 15000000;
constexpr uint64_t kScanBlinkUs = 250000;
uint64_t reg_stage_us = 0;     // STARTUP / SCAN に入った時刻
uint64_t bootsel_poll_us = 0;
uint64_t bootsel_down_us = 0;  // 押し始めた時刻 (0 = 押していない)
bool bootsel_fired = false;    // 長押し成立済み (離すまで再発火しない)
bool bootsel_indicated = false;
bool bootsel_available = true; // gpio object が無い等で読めなければ以後読まない
uint64_t blink_us = 0;
bool blink_on = false;
// BLE 一回券の残り監視 / 承認待ち表示 (承認そのものはカーネルが BOOTSEL でやる)。
constexpr uint64_t kPairPollUs = 200000;
constexpr uint64_t kPairBlinkUs = 300000;
constexpr uint64_t kPairTicketUs = 180000000;
uint64_t pair_ticket_until_us = 0; // 0 = 一回券なし (発行から 3 分の目安)
uint64_t pair_poll_us = 0;
uint64_t pair_blink_us = 0;
bool pair_pending = false;         // カーネルが BOOTSEL 承認待ち (nc_pending=1)
bool pair_blink_on = false;
uint32_t pair_pending_gen = 0;
uint8_t bond_count = 0;
uint8_t first_bond[6] = {};
bool bond_is_default = false;

void connect_to(const uint8_t *addr) {
  std::printf("[CAR] connect %02x:%02x:%02x:%02x:%02x:%02x\n", addr[0], addr[1], addr[2],
              addr[3], addr[4], addr[5]);
  send_frame(bc::TX_CONNECT, addr, 6);
}

void enter_normal_and_connect_default(const char *why) {
  std::printf("[CAR] reregister: back to default target (%s)\n", why);
  reg_mode = REG_NORMAL;
  connect_to(kRemoteAddr);
}

void start_reregister() {
  std::printf("[CAR] BOOTSEL long press: forget bonds and scan for a new controller\n");
  stop_motors();
  send_frame(bc::TX_FORGET, nullptr, 0);
  const uint8_t sec = kScanSeconds;
  send_frame(bc::TX_SCAN_START, &sec, 1);
  reg_mode = REG_SCAN;
  reg_stage_us = now_us();
  blink_us = reg_stage_us;
  blink_on = true;
  call(id::io, (uintptr_t)io_method::set_central, urgb_u32(255, 160, 0)); // 琥珀 = 探索中
}

// ゲームパッド / ジョイスティック (Peripheral の major 0x05、minor の bit2..5 が 1 or 2)。
bool cod_is_gamepad(const uint8_t *cod) {
  const uint32_t v = ((uint32_t)cod[0] << 16) | ((uint32_t)cod[1] << 8) | cod[2];
  const uint32_t major = (v >> 8) & 0x1F;
  const uint32_t minor = (v >> 2) & 0x0F;
  return major == 0x05 && (minor == 0x01 || minor == 0x02);
}

bool read_bootsel(bool &pressed) {
  const auto r = call(shizuku::object_id::gpio,
                      (uintptr_t)shizuku::objects::gpio_method::BOOTSEL_READ_PRESSED);
  if (r.error != 0) return false;
  pressed = r.value != 0;
  return true;
}

// 一回券がある間だけ GET_PAIRING_STATE を 200 ms ごとに読み、承認待ちなら紫点滅。
// ★車は PAIRING_ANSWER を呼ばない。承認はカーネルが BOOTSEL の押して離すで行う。
// ★紫 (255,0,255) は琥珀 (登録探索) / 青 (一回券) と区別するための色。
void ble_pair_poll() {
  const uint64_t t = now_us();
  if (pair_ticket_until_us == 0 && !pair_pending) return;
  if (pair_pending && t - pair_blink_us >= kPairBlinkUs) {
    pair_blink_us = t;
    pair_blink_on = !pair_blink_on;
    call(id::io, (uintptr_t)io_method::set_central,
         pair_blink_on ? urgb_u32(255, 0, 255) : 0);
  }
  if (t - pair_poll_us < kPairPollUs) return;
  pair_poll_us = t;
  shizuku::objects::ble_uart::pairing_state st{};
  const auto r = call(shizuku::object_id::ble_uart,
      (uintptr_t)shizuku::objects::ble_uart::method::GET_PAIRING_STATE, (uintptr_t)&st);
  if (r.error != 0 || r.value == 0) return;
  if (st.nc_pending != 0) {
    if (!pair_pending || st.nc_generation != pair_pending_gen) {
      std::printf("[CAR] BLE pairing request (num=%06lu): press BOOTSEL to approve\n",
                  (unsigned long)st.nc_passkey);
      pair_pending_gen = st.nc_generation;
    }
    pair_pending = true;
    return;
  }
  if (pair_pending) { // 承認・拒否・時間切れのどれかで終わった
    pair_pending = false;
    pair_blink_on = false;
    call(id::io, (uintptr_t)io_method::set_central, hid_open ? urgb_u32(0, 0, 255) : 0);
  }
  if (st.allow_seconds_left == 0 && t >= pair_ticket_until_us)
    pair_ticket_until_us = 0; // 一回券を使った / 期限切れ
}

// 3 秒で琥珀、離せば登録し直し。10 秒で BLE 一回券を発行し青点滅。
void bootsel_poll() {
  if (!bootsel_available || reg_mode != REG_NORMAL) return;
  const uint64_t t = now_us();
  // BLE 承認待ちの間は BOOTSEL をカーネルの承認に譲る (3 秒 / 10 秒処理を動かさない)。
  if (pair_pending) {
    bootsel_down_us = 0;
    bootsel_fired = false;
    bootsel_indicated = false;
    return;
  }
  if (t - bootsel_poll_us < kBootselPollUs) return;
  bootsel_poll_us = t;
  bool pressed = false;
  if (!read_bootsel(pressed)) {
    std::printf("[CAR] BOOTSEL read unavailable (gpio object / SHIZUKU_ENABLE_BOOTSEL?)\n");
    bootsel_available = false;
    return;
  }
  if (!pressed) {
    const bool reregister = bootsel_down_us != 0 && bootsel_indicated && !bootsel_fired;
    if (bootsel_indicated || bootsel_fired)
      call(id::io, (uintptr_t)io_method::set_central, hid_open ? urgb_u32(0, 0, 255) : 0);
    bootsel_down_us = 0;
    bootsel_fired = false;
    bootsel_indicated = false;
    if (reregister) start_reregister();
    return;
  }
  if (bootsel_down_us == 0) bootsel_down_us = t;
  const uint64_t held = t - bootsel_down_us;
  if (!bootsel_fired && held >= kBootselBleUs) {
    bootsel_fired = true;
    const auto result = call(shizuku::object_id::ble_uart,
        (uintptr_t)shizuku::objects::ble_uart::method::SET_PAIRING_LOCK, 0);
    if (result.error == 0 && result.value != 0)
    {
      std::printf("[CAR] BOOTSEL 10s: BLE pairing allow ticket (3 minutes)\n");
      pair_ticket_until_us = t + kPairTicketUs;
    }
    else
      std::printf("[CAR] BOOTSEL 10s: BLE pairing allow failed\n");
  }
  if (bootsel_fired) {
    call(id::io, (uintptr_t)io_method::set_central,
         ((held - kBootselBleUs) / kScanBlinkUs) % 2 == 0 ? urgb_u32(0, 0, 255) : 0);
  } else if (!bootsel_indicated && held >= kBootselHoldUs) {
    bootsel_indicated = true;
    call(id::io, (uintptr_t)io_method::set_central, urgb_u32(255, 160, 0));
  }
}

// 起動時の bond 問い合わせの待ち切れ、スキャンの暴走防止、探索中の LED 点滅。
void reg_poll() {
  const uint64_t t = now_us();
  if (reg_mode == REG_STARTUP && t - reg_stage_us > kBondQueryTimeoutUs) {
    std::printf("[CAR] no RX_BOND_END (kernel without bond IF?)\n");
    enter_normal_and_connect_default("bond query timeout");
  } else if (reg_mode == REG_SCAN) {
    if (t - reg_stage_us > kScanGuardUs) {
      send_frame(bc::TX_SCAN_STOP, nullptr, 0);
      enter_normal_and_connect_default("scan guard timeout");
    } else if (t - blink_us >= kScanBlinkUs) {
      blink_us = t;
      blink_on = !blink_on;
      call(id::io, (uintptr_t)io_method::set_central,
           blink_on ? urgb_u32(255, 160, 0) : 0);
    }
  }
}

void on_reg_frame(const hid_frame &f) {
  switch (f.kind) {
  case bc::RX_BOND:
    if (f.len < 6) break;
    std::printf("[CAR] bond %02x:%02x:%02x:%02x:%02x:%02x\n", f.data[0], f.data[1],
                f.data[2], f.data[3], f.data[4], f.data[5]);
    if (bond_count == 0 || (!bond_is_default && memcmp(f.data, kRemoteAddr, 6) == 0)) {
      memcpy(first_bond, f.data, 6); // 既定アドレスの bond があればそれを優先、なければ最初の 1 件
      bond_is_default = memcmp(f.data, kRemoteAddr, 6) == 0;
    }
    if (bond_count < 255) ++bond_count;
    break;
  case bc::RX_BOND_END:
    if (reg_mode != REG_STARTUP) break;
    std::printf("[CAR] bonds=%u\n", (unsigned)bond_count);
    reg_mode = REG_NORMAL;
    connect_to(bond_count != 0 ? first_bond : kRemoteAddr);
    break;
  case bc::RX_SCAN_FOUND:
    if (reg_mode != REG_SCAN || f.len < 9) break;
    std::printf("[CAR] scan found %02x:%02x:%02x:%02x:%02x:%02x cod=%02x%02x%02x\n",
                f.data[0], f.data[1], f.data[2], f.data[3], f.data[4], f.data[5],
                f.data[6], f.data[7], f.data[8]);
    if (cod_is_gamepad(&f.data[6])) {
      reg_mode = REG_PAIRING;
      call(id::io, (uintptr_t)io_method::set_central, urgb_u32(255, 255, 255)); // 白 = 接続中
      connect_to(f.data); // カーネルが inquiry を止めて、完了後にこの相手へ接続する
    }
    break;
  case bc::RX_SCAN_DONE:
    std::printf("[CAR] scan done reason=%u\n", f.len ? (unsigned)f.data[0] : 0u);
    if (reg_mode == REG_SCAN) {
      call(id::io, (uintptr_t)io_method::set_central, 0);
      enter_normal_and_connect_default("no gamepad found");
    } else if (reg_mode == REG_PAIRING) {
      reg_mode = REG_NORMAL; // 以後の接続はカーネルが target (見つけた相手) で続ける
    }
    break;
  default:
    break;
  }
}

void on_frame(const hid_frame &f) {
  switch (f.kind) {
  case bc::RX_BOND:
  case bc::RX_BOND_END:
  case bc::RX_SCAN_FOUND:
  case bc::RX_SCAN_DONE:
    on_reg_frame(f);
    return;
  default:
    break;
  }
  switch (f.kind) {
  case bc::RX_OPENED:
    std::printf("[CAR] HID opened\n");
    if (reg_mode == REG_PAIRING) reg_mode = REG_NORMAL;
    hid_open = true;
    init_started = false;
    init_state = INIT_IDLE;
    call(id::io, (uintptr_t)io_method::set_central, urgb_u32(0, 0, 255));
    break;
  case bc::RX_CLOSED:
    std::printf("[CAR] HID closed (reason=0x%02x)\n", f.len ? f.data[0] : 0);
    hid_open = false;
    init_started = false;
    init_state = INIT_IDLE;
    dpad_up_prev = dpad_down_prev = false;
    stop_motors();
    call(id::io, (uintptr_t)io_method::set_central, 0);
    break;
  case bc::RX_REPORT:
    if (reg_mode == REG_SCAN) break; // 切断イベントが来るまでの残りの入力でモーターを動かさない
    on_report(f.data, f.len);
    break;
  default:
    break;
  }
}

uintptr_t hid_poll(uintptr_t, uintptr_t, uintptr_t, uintptr_t) {
  using shizuku::objects::ble_uart::method;
  const uintptr_t ble = shizuku::object_id::ble_uart;
  // ★席に座るのはこのスレッド (STREAM_BIND は発行元オブジェクトから席を導出する)。
  api(shizuku::object_api::STREAM_BIND, g_tx_id,
      (uintptr_t)shizuku::stream::role::PRODUCER);
  const auto rx_id = call(ble, (uintptr_t)method::GET_HID_RX_STREAM);
  if (rx_id.error != 0 || rx_id.value == 0) {
    std::printf("[CAR] no hid_rx stream (BT core without SHIZUKU_BT_CLASSIC?)\n");
    return 1;
  }
  const auto opened = api(shizuku::object_api::STREAM_OPEN, rx_id.value);
  if (opened.error != 0 || opened.value == 0) {
    std::printf("[CAR] hid_rx open failed (%lu)\n", (unsigned long)opened.error);
    return 1;
  }
  g_rx = shizuku::stream::handle<hid_frame>(opened.value);
  api(shizuku::object_api::STREAM_BIND, rx_id.value,
      (uintptr_t)shizuku::stream::role::CONSUMER);

  // まず保存済みの相手 (bond) を尋ねる。RX_BOND_END で接続先 (bond があればその相手、
  // なければ既定の REMOTE_BD_ADDR) を TX_CONNECT する (on_reg_frame)。接続は BT core が
  // HCI_STATE_WORKING のイベントから始め、再試行も BTstack のタイマーで行う。
  reg_mode = REG_STARTUP;
  reg_stage_us = now_us();
  send_frame(bc::TX_LIST_BONDS, nullptr, 0);

  hid_frame f;
  while (true) {
    while (g_rx.available() != 0 && g_rx.pop(&f)) on_frame(f);
    init_poll();
    bootsel_poll();
    ble_pair_poll();
    reg_poll();
    // 0x3F は変化があったときしか来ないので、毎周期流れる 0x30 に入った後だけ
    // 無通信を異常とみなす。
    if (motors_active && init_state == INIT_DONE &&
        now_us() - last_report_us > kReportTimeoutUs) {
      std::printf("[CAR] controller report timeout; stopping motors\n");
      stop_motors();
    }
    api(shizuku::object_api::SLEEP_US, 1000);
  }
}
} // namespace

uintptr_t hid_tx_stream_id() { return g_tx_id; }

uintptr_t hid_main(uintptr_t, uintptr_t, uintptr_t, uintptr_t) {
  const auto tx = api(shizuku::object_api::STREAM_CREATE, sizeof(hid_frame), 16, 0);
  if (tx.error != 0) return 1;
  g_tx_id = tx.value;
  g_tx = shizuku::stream::handle<hid_frame>(g_tx_id);
  return api(shizuku::object_api::EXPORT_METHOD, (uintptr_t)hid_method::poll,
             (uintptr_t)&hid_poll).error;
}
} // namespace car
