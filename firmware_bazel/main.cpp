#include "car_objects.hpp"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "shizuku/app_entry.hpp"
#include "shizuku/kernel.hpp"
#include "shizuku/kernel_object.hpp"
#include "shizuku/object_api.hpp"
#include "shizuku/objects/ble_uart.hpp"
#include "shizuku/objects/peripherals.hpp"
#include "shizuku/objects/usb_cdc.hpp"
#include "objects/ota/ota.hpp" // @xno//objects/ota (xno::ota)

void shizuku::app_entry() {
  using shizuku::objects::ble_uart::method;
  namespace ble = shizuku::objects::ble_uart;
  // OTA 後に新しい像が走っているかをログで確かめるための印 (CDC の先頭付近に出る)。
  shizuku::KERNEL::BOARD::diag_printf("[CAR] build: %s %s\n", __DATE__, __TIME__);
  shizuku::kernel_instance.set_recovery_thread(0);
  // gpio object だけ登録する (BOOTSEL 長押しの読み取り用。led/spi/i2c は車では使わない)。
  const uint32_t gpio_failures = shizuku::objects::register_gpio();
  const uintptr_t failures = car::register_objects() + gpio_failures;
  shizuku::KERNEL::BOARD::diag_printf("[CAR] object startup failures=%lu\n",
                                       (unsigned long)failures);

  // Classic の link key / BLE の bonding は BTstack が flash TLV に保存する。保存
  // (flash_safe_execute) はもう一方のコアが lockout victim でないと assert で止まるので、
  // BT core を起こす前に 2 本目のコアを起こして victim になるのを待つ。
  bool core1_ready = shizuku::kernel_object_instance.start_secondary_core();
  if (core1_ready) {
    const uint64_t deadline_us = time_us_64() + 1000000ull;
    while (!multicore_lockout_victim_is_initialized(1) && time_us_64() < deadline_us)
      tight_loop_contents();
    core1_ready = multicore_lockout_victim_is_initialized(1);
  }
  if (!core1_ready) {
    shizuku::KERNEL::BOARD::diag_printf("[CAR] core 1 unavailable: BT core not started\n");
  } else {
    // BT core = ble_uart (CYW43 の唯一の owner。BLE NUS + Classic HID host)。
    constexpr uintptr_t kBle = shizuku::object_id::ble_uart;
    if (ble::register_ble_uart(kBle) != 0) {
      shizuku::KERNEL::BOARD::diag_printf("[CAR] register_ble_uart failed\n");
    } else {
      // ★poll 起動前に hid_tx の番号を渡す (席は poll スレッドの冒頭で座る)。
      car::call(kBle, (uintptr_t)method::SET_HID_TX_STREAM, car::hid_tx_stream_id());

      // BLE OTA (xno::ota)。結線は XNO firmware/rp_ota_main.cpp の bring_up_ble_ota() と同じ:
      //   ble_uart の OTA 受信ストリーム -> ota の入力 / ota の進捗ストリーム -> ble_uart の TX。
      // OTA の登録に失敗しても車の走行 (Classic HID) は止めない。その場合は OTA だけ無効。
      constexpr uintptr_t kOta = shizuku::object_id::ota;
      const uint32_t ota_rc = xno::ota::register_ota(kOta, 0, kBle);
      if (ota_rc != 0) {
        shizuku::KERNEL::BOARD::diag_printf("[CAR] register_ota failed: %lu (OTA disabled)\n",
                                             (unsigned long)ota_rc);
      } else {
        const auto ota_rx = car::call(kBle, (uintptr_t)method::GET_OTA_STREAM);
        if (ota_rx.error == 0 && ota_rx.value != 0)
          car::call(kOta, (uintptr_t)xno::ota::method::SET_INPUT_STREAM, ota_rx.value);
        const auto ota_tx = car::call(kOta, (uintptr_t)xno::ota::method::GET_STREAM);
        if (ota_tx.error == 0 && ota_tx.value != 0)
          car::call(kBle, (uintptr_t)method::SET_TX_STREAM, ota_tx.value);
      }

      ble::start_ble_uart(kBle);
      if (ota_rc == 0)
        xno::ota::start_ota(kOta);
      car::api(shizuku::object_api::SPAWN, car::id::classic_hid,
               (uintptr_t)car::hid_method::poll);
    }
  }
  while (true)
    car::api(shizuku::object_api::YIELD);
}

int main() {
  shizuku::objects::usb_cdc_init();
  sleep_ms(1000);
  shizuku::kernel_instance.init();
  shizuku::kernel_object_instance.init();
  shizuku::kernel_instance.set_object_handler(
      shizuku::KERNEL_OBJECT::handler_entry(),
      (uint32_t)shizuku::KERNEL_OBJECT::KERNEL_OBJECT_ID);
  const auto boot = shizuku::kernel_object_instance.lend_boot_stack();
  shizuku::kernel_instance.bootstrap(shizuku::app_entry, boot.base, boot.bytes,
                                     boot.ledger, boot.ledger_capacity);
}
