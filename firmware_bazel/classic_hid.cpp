#include "car_objects.hpp"
#include "btstack_config.h"
extern "C" {
#include "btstack.h"
#include "classic/hid_host.h"
#include "classic/sdp_server.h"
#include "gap.h"
#include "hci.h"
#include "l2cap.h"
}
#include <pico/cyw43_arch.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "shizuku/kernel.hpp"

namespace car {

// WS2812 は GRB 順 (旧 CYW43_BL_DRIVER.cpp と同じ)。
static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint32_t)(r) << 8) | ((uint32_t)(g) << 16) | (uint32_t)(b);
}

static btstack_packet_callback_registration_t hci_event_callback_registration;
static uint8_t hid_descriptor_storage[512];
#ifndef REMOTE_BD_ADDR
#define REMOTE_BD_ADDR 0x98, 0xB6, 0xB4, 0x74, 0xB1, 0x55
#endif
static bd_addr_t remote_addr = {REMOTE_BD_ADDR};
static const uint8_t neutral_rumble[8] = {0x00, 0x01, 0x40, 0x40,
                                          0x00, 0x01, 0x40, 0x40};

enum InitState {
  INIT_IDLE,
  INIT_SET_MODE,
  INIT_ENABLE_VIB,
  INIT_SET_LED,
  INIT_DONE
};

static char pin_code[] = "0000";
static uint16_t hid_cid = 0;
static bool hid_protocol_set = false; // プロトコルモード設定フラグ
static uint8_t output_counter = 0;
static InitState init_state = INIT_IDLE;
static bool init_started = false;
// 入力が途切れたら車体を止める。切断イベントが来ない無線断 (電波切れ・コントローラの
// 電池切れ) でも、最後の指令のまま走り続けないようにする。元の CMake 版には無い。
constexpr uint64_t kReportTimeoutUs = 500000;
static uint64_t last_report_us = 0;
static bool motors_active = false;
static uint32_t motor_log_divider = 0;

static void stop_motors() {
  constexpr uintptr_t motor_ids[] = {id::motor_fl, id::motor_fr, id::motor_rl,
                                     id::motor_rr};
  for (const uintptr_t motor : motor_ids)
    call(motor, (uintptr_t)motor_method::set_speed, 1000);
  motors_active = false;
}

// サブコマンド送信用の関数
static void send_subcommand(uint16_t hid_cid, uint8_t subcommand, uint8_t *args,
                            uint8_t arg_len) {
  uint8_t payload[64];
  memset(payload, 0, sizeof(payload));

  // Nintendo Switch Pro Controller サブコマンド形式：
  // report_idが先頭に付与される（hid_host_send_set_report経由）
  // [0]: Packet Counter (report本体の先頭)
  // [1-8]: Rumble (8 bytes) - neutral: 0x00,0x01,0x40,0x40,0x00,0x01,0x40,0x40
  // [9]: SubcommandID
  // [10+]: Arguments

  payload[0] = output_counter & 0x0F;
  output_counter++;

  // ニュートラルランブル設定
  payload[1] = 0x00;
  payload[2] = 0x01;
  payload[3] = 0x40;
  payload[4] = 0x40;
  payload[5] = 0x00;
  payload[6] = 0x01;
  payload[7] = 0x40;
  payload[8] = 0x40;

  // サブコマンド
  payload[9] = subcommand;
  if (arg_len > 0 && args != nullptr) {
    memcpy(&payload[10], args, arg_len);
  }

  uint16_t payload_len = 10 + arg_len;

  printf("Sending HID SET_REPORT subcommand 0x%02x (len=%d): ", subcommand,
         payload_len);
  for (int i = 0; i < payload_len; i++) {
    printf("%02x ", payload[i]);
  }
  printf("\n");

  // BTStack HID APIを使用
  // 第3引数のreport_idで0x01を指定すると、先頭に0x01が自動付与される
  // 最終的にプロコンに届くデータ：
  // [0x01] + payload = [0x01][packet_counter][rumble][subcommand][args]
  uint8_t send_result = hid_host_send_set_report(
      hid_cid, hid_report_type_t::HID_REPORT_TYPE_OUTPUT, 0x01, payload,
      payload_len);
  if (send_result == ERROR_CODE_SUCCESS) {
    printf("send_success\n");
  } else {
    printf("send_failed\n");
  }
}

// LED設定
static void send_player_led(uint16_t hid_cid) {
  uint8_t arg = 0x0F; // LED1 ON
  send_subcommand(hid_cid, 0x30, &arg, 1);
}

// 振動機能有効化
static void send_enable_vibration(uint16_t hid_cid) {
  uint8_t arg = 0x01; // Enable
  send_subcommand(hid_cid, 0x48, &arg, 1);
}

// 入力モード設定
static void send_set_input_report_mode(uint16_t hid_cid) {
  uint8_t arg = 0x30; // standard full mode
  send_subcommand(hid_cid, 0x03, &arg, 1);
}

static void packet_handler(uint8_t packet_type, uint16_t channel,
                           uint8_t *packet, uint16_t size) {
  if (packet_type != HCI_EVENT_PACKET)
    return;

  uint8_t event = hci_event_packet_get_type(packet);

  switch (event) {

  case BTSTACK_EVENT_STATE: {
    printf("BTSTACK_EVENT_STATE\n");
    if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
      // HCI ready → 接続開始
      hid_host_connect(remote_addr, HID_PROTOCOL_MODE_REPORT, &hid_cid);
    }
    break;
  }

  case HCI_EVENT_PIN_CODE_REQUEST: {
    bd_addr_t addr;
    hci_event_pin_code_request_get_bd_addr(packet, addr);
    printf("HCI_EVENT_PIN_CODE_REQUEST\naddr: %02x:%02x:%02x:%02x:%02x:%02x\n",
           addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    gap_pin_code_response(addr, pin_code);
    break;
  }

  case HCI_EVENT_USER_CONFIRMATION_REQUEST: {
    bd_addr_t addr;
    hci_event_user_confirmation_request_get_bd_addr(packet, addr);
    printf("HCI_EVENT_USER_CONFIRMATION_REQUEST\naddr: "
           "%02x:%02x:%02x:%02x:%02x:%02x\n",
           addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    gap_ssp_confirmation_response(addr);
    break;
  }

  case HCI_EVENT_LINK_KEY_REQUEST: {
    bd_addr_t addr;
    hci_event_link_key_request_get_bd_addr(packet, addr);
    printf("HCI_EVENT_LINK_KEY_REQUEST\naddr: %02x:%02x:%02x:%02x:%02x:%02x\n",
           addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    // 保存していないので否定
    // gap_send_link_key_response(addr,NULL,INVALID_LINK_KEY);
    hci_send_cmd(&hci_link_key_request_negative_reply, addr);
    break;
  }

  case HCI_EVENT_LINK_KEY_NOTIFICATION: {
    // RAMに保存するならここ
    printf("HCI_EVENT_LINK_KEY_NOTIFICATION\n");
    break;
  }

  case HCI_EVENT_AUTHENTICATION_COMPLETE: {
    if (hci_event_authentication_complete_get_status(packet) !=
        ERROR_CODE_SUCCESS) {
      // 認証失敗
      printf("HCI_EVENT_AUTHENTICATION_COMPLETE:Failed\n");
      hid_host_connect(remote_addr, HID_PROTOCOL_MODE_REPORT, &hid_cid);
    } else {
      printf("HCI_EVENT_AUTHENTICATION_COMPLETE:Success\n");
    }
    break;
  }

  case HCI_EVENT_CONNECTION_COMPLETE: {
    if (hci_event_connection_complete_get_status(packet) !=
        ERROR_CODE_SUCCESS) {
      // 接続失敗
      printf("HCI_EVENT_CONNECTION_COMPLETE:Not Success\n");
    } else {
      printf("HCI_EVENT_CONNECTION_COMPLETE:Success\n");
    }
    break;
  }

  case HCI_EVENT_DISCONNECTION_COMPLETE: {
    // デモ用途なら即再接続
    hid_cid = 0;
    hid_protocol_set = false;
    init_started = false;
    init_state = INIT_IDLE;
    stop_motors();
    printf("HCI_EVENT_DISCONNECTION_COMPLETE\nreconnecting...\n");
    hid_host_connect(remote_addr, HID_PROTOCOL_MODE_REPORT, &hid_cid);
    break;
  }

  case HCI_EVENT_HID_META: {
    // printf("HCI_EVENT_HID_META\n");
    switch (hci_event_hid_meta_get_subevent_code(packet)) {
    case HID_SUBEVENT_REPORT: {
      const uint8_t *report = hid_subevent_report_get_report(packet);
      uint16_t len = hid_subevent_report_get_report_len(packet);
      if (len >= 13 && report[0] == 0xA1 &&
          (report[1] == 0x3F || report[1] == 0x21 || report[1] == 0x30)) {
        uint8_t report_id = report[1];
        int32_t lx, ly, rx, ry;
        if (report_id == 0x3F) {
          // Standard Mode (0x3F): 16-bit values. Shift to 12-bit scale (0-4095)
          // for display.
          lx = (report[5] | (report[6] << 8)) >> 4;
          ly = (report[7] | (report[8] << 8)) >> 4;
          rx = (report[9] | (report[10] << 8)) >> 4;
          ry = (report[11] | (report[12] << 8)) >> 4;
        } else {
          // Full Mode (0x30): 12-bit packed values.
          lx = report[7] | ((report[8] & 0xf) << 8);
          ly = ((report[8] & 0xf0) >> 4) | (report[9] << 4);
          rx = report[10] | ((report[11] & 0xf) << 8);
          ry = ((report[11] & 0xf0) >> 4) | (report[12] << 4);
        }
        // printf("\033[2K\rStick: %4ld %4ld %4ld %4ld\n", lx, ly, rx, ry);
        lx -= 2048;
        ly -= 2048;
        rx -= 2048;
        ry -= 2048;

        double FL = (rx - (ly + ry) + lx) / 2;
        double FR = (-rx - (ly + ry) - lx) / 2;
        double RL = (-rx - (ly + ry) + lx) / 2;
        double RR = (+rx - (ly + ry) - lx) / 2;

        double maxVal = fabs(FL);

        if (fabs(FR) > maxVal)
          maxVal = fabs(FR);
        if (fabs(RL) > maxVal)
          maxVal = fabs(RL);
        if (fabs(RR) > maxVal)
          maxVal = fabs(RR);
        if (maxVal > 1000) {

          FL /= maxVal;
          FR /= maxVal;
          RL /= maxVal;
          RR /= maxVal;
          FL *= 1000;
          FR *= 1000;
          RL *= 1000;
          RR *= 1000;
        }
        call(id::motor_fl, (uintptr_t)motor_method::set_speed,
             (uintptr_t)(FL + 1000));
        call(id::motor_fr, (uintptr_t)motor_method::set_speed,
             (uintptr_t)(FR + 1000));
        call(id::motor_rl, (uintptr_t)motor_method::set_speed,
             (uintptr_t)(RL + 1000));
        call(id::motor_rr, (uintptr_t)motor_method::set_speed,
             (uintptr_t)(RR + 1000));
        last_report_us = shizuku::KERNEL::BOARD::time_us();
        motors_active = true;
        // 1 レポートごとに CDC へ書くと割込み禁止区間が増えるので間引く。
        if (++motor_log_divider % 32 == 0)
          printf("Motor: FL:%4ld FR:%4ld RL:%4ld RR:%4ld\n", (int32_t)FL,
                 (int32_t)FR, (int32_t)RL, (int32_t)RR);

        // obj_api::yield_us(10000);

        // ① 初期状態 (3F) のデータが来た！
        if (report_id == 0x3F) {
          // プロトコルモード設定後に初期化を開始
          if (!init_started && hid_protocol_set) {
            printf("Simple Mode (3F) detected! Starting initialization...\n");
            init_started = true;
            init_state = INIT_SET_MODE;
            send_set_input_report_mode(hid_cid);
          }
        }
        // ② サブコマンドへの返事 (21) が来た！
        else if (report_id == 0x21 && len >= 15) {
          uint8_t ack = report[13];         // ACK byte
          uint8_t replied_cmd = report[14]; // Replied subcommand ID

          if ((ack & 0x80) != 0) {
            printf("ACK received for cmd: 0x%02x\n", replied_cmd);

            // ステートマシンに基づいて次のコマンドを送信
            if (init_state == INIT_SET_MODE && replied_cmd == 0x03) {
              printf("Input mode set to full mode. Enabling vibration...\n");
              init_state = INIT_ENABLE_VIB;
              send_enable_vibration(hid_cid);
            } else if (init_state == INIT_ENABLE_VIB && replied_cmd == 0x48) {
              printf("Vibration enabled. Setting LED...\n");
              init_state = INIT_SET_LED;
              send_player_led(hid_cid);
            } else if (init_state == INIT_SET_LED && replied_cmd == 0x30) {
              printf("LED set. Initialization complete!\n");
              init_state = INIT_DONE;
            }
          } else {
            printf("NAK received for cmd: 0x%02x\n", replied_cmd);
          }
        }
        // ③ フルモード (30) のデータが来た！
        else if (report_id == 0x30) {
          if (init_state == INIT_DONE) {
            printf("Full Mode report received. Controller ready!\n");
          }
        }
      }
      break;
    }
    case HID_SUBEVENT_SET_REPORT_RESPONSE: {
      printf("HID_SUBEVENT_SET_REPORT_RESPONSE\n");
      auto status =
          hid_subevent_set_report_response_get_handshake_status(packet);
      if (status == HID_HANDSHAKE_PARAM_TYPE_SUCCESSFUL) {
        printf("HID_HANDSHAKE_PARAM_TYPE_SUCCESSFUL\n");
        // フロー制御はHID_SUBEVENT_REPORT側で行うため、ここでは送信しない
        // send_player_led(hid_cid);
      }
      break;
    }
    case HID_SUBEVENT_CONNECTION_OPENED:
      printf("HID_SUBEVENT_CONNECTION_OPENED\n");
      if (hid_subevent_connection_opened_get_status(packet) ==
          ERROR_CODE_SUCCESS) {
        hid_cid = hid_subevent_connection_opened_get_hid_cid(packet);
        printf("HID Connected! HID CID: %04x\n", hid_cid);
        call(id::ws2812,
                    (uintptr_t)pixel_method::set_central,
                    urgb_u32(0, 0, 255));

        // プロトコルモードを明示的に設定（シンプルモード→レポートモード）
        printf("Setting HID protocol mode to REPORT...\n");
        hid_host_send_set_protocol_mode(hid_cid, HID_PROTOCOL_MODE_REPORT);
        hid_protocol_set = true;
      }
      break;
    }
    break;
  }

  default:
    break;
  }
}

uintptr_t hid_poll(uintptr_t, uintptr_t, uintptr_t, uintptr_t) {

  if (cyw43_arch_init() != 0) {
    shizuku::KERNEL::BOARD::diag_printf("[CAR] CYW43 init failed\n");
    return 1;
  }

  l2cap_init();
  sdp_init();
  hid_host_init(hid_descriptor_storage, sizeof(hid_descriptor_storage));

  hid_host_register_packet_handler(packet_handler);

  hci_event_callback_registration.callback = &packet_handler;
  hci_add_event_handler(&hci_event_callback_registration);
  hci_power_control(HCI_POWER_ON);

  while (true) {
    // Also poll the cyw43 driver
    cyw43_arch_poll();
    // 0x3F (簡易モード) は変化があったときしか送られないので、毎周期レポートが
    // 流れる 0x30 に入った (INIT_DONE) 後だけ無通信を異常とみなす。
    if (motors_active && init_state == INIT_DONE &&
        shizuku::KERNEL::BOARD::time_us() - last_report_us > kReportTimeoutUs) {
      printf("[CAR] controller report timeout; stopping motors\n");
      stop_motors();
    }
    // 他のスレッドにCPU時間を譲る
    api(shizuku::object_api::SLEEP_US, 1000);
  }
}

uintptr_t hid_main(uintptr_t, uintptr_t, uintptr_t, uintptr_t) {
  return api(shizuku::object_api::EXPORT_METHOD, (uintptr_t)hid_method::poll,
             (uintptr_t)&hid_poll).error;
}

} // namespace car
