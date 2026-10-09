#include "car_objects.hpp"
#include "pico/stdlib.h"
#include "shizuku/app_entry.hpp"
#include "shizuku/kernel.hpp"
#include "shizuku/kernel_object.hpp"
#include "shizuku/object_api.hpp"
#include "shizuku/objects/usb_cdc.hpp"

void shizuku::app_entry() {
  shizuku::kernel_instance.set_recovery_thread(0);
  const uintptr_t failures = car::register_io();
  shizuku::KERNEL::BOARD::diag_printf("[CAR] object startup failures=%lu\n",
                                       (unsigned long)failures);
  // pico-sdk の btstack_cyw43_init() は Classic の link key を flash の TLV に
  // 張る。保存 (flash_safe_execute) はもう一方のコアを止められる状態でなければ
  // assert で止まるので、firmware/main.cpp と同じく 2 本目のコアを起こしておく。
  // 車のオブジェクトはすべて OBJECT_ON_CORE(0) なのでコア 1 では何も走らない。
  if (shizuku::kernel_object_instance.start_secondary_core())
    shizuku::KERNEL::BOARD::diag_printf("[CAR] secondary core launched\n");
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
