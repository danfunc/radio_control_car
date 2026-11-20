#ifndef RADIO_CONTROL_CAR_OBJECTS_HPP
#define RADIO_CONTROL_CAR_OBJECTS_HPP

#include <cstdint>
#include "shizuku/config.hpp"
#include "shizuku/kernel.hpp"
#include "shizuku/object_api.hpp"
#include "shizuku/object_ids.hpp"

namespace car {
// 新カーネルのオブジェクト番号は objects.list からビルドが振り、全体の上限は 72。
// --define=skip_selftest=true なら selftest の名前は番号に入らず (count=14)、空きは十分ある。
// 周辺 (モータ/LED/ライン) は 1 本の car_io にまとめてある: 台数ごとの GET_CURRENT_OBJECT
// 分岐と、モータ→LED のオブジェクト越し呼び出しが要らず単純になるため (空き不足の暫定ではない)。
namespace id {
constexpr uintptr_t base = shizuku::object_id::count;
constexpr uintptr_t io = base;
constexpr uintptr_t classic_hid = base + 1;
static_assert(classic_hid < shizuku::KERNEL_OBJECT::OBJECT_COUNT,
              "Shizuku object table has no room for the car objects");
}

enum class io_method : uintptr_t {
  main = 0,
  set_motor = 1,   // a1 = (position << 16) | (speed + 1000)。position 0..3 = FL FR RL RR
  set_pixel = 2,   // a1 = pixel_arg(position, grb)
  set_central = 3, // a1 = grb
  poll = 4,        // WS2812 の送出スレッド (SPAWN する)
  line_read = 5,
};
enum class hid_method : uintptr_t { main = 0, poll = 1 };

struct result { uintptr_t error; uintptr_t value; };
inline result api(shizuku::object_api number, uintptr_t a1 = 0,
                  uintptr_t a2 = 0, uintptr_t a3 = 0) {
  const auto r = shizuku::KERNEL::ARCH::syscall((uintptr_t)number, a1, a2, a3);
  return {r.error, r.value};
}
inline result call(uintptr_t object, uintptr_t method, uintptr_t argument = 0) {
  return api(shizuku::object_api::CALL_METHOD, object, method, argument);
}
constexpr uintptr_t pixel_arg(uintptr_t position, uintptr_t grb) {
  return (position << 24) | (grb & 0xFFFFFFu);
}
constexpr uintptr_t motor_arg(uintptr_t position, uintptr_t raw) {
  return (position << 16) | (raw & 0xFFFFu);
}

uintptr_t io_main(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
uintptr_t hid_main(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
// classic_hid が作る hid_tx ストリーム番号 (hid_main の後で有効)。app_entry が
// ble_uart の SET_HID_TX_STREAM へ渡す (ble_uart の poll 起動前に)。
uintptr_t hid_tx_stream_id();
// 車のオブジェクトを作る。失敗数を返す。
uintptr_t register_objects();
}
#endif
