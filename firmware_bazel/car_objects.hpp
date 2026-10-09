#ifndef RADIO_CONTROL_CAR_OBJECTS_HPP
#define RADIO_CONTROL_CAR_OBJECTS_HPP

#include <cstdint>
#include "shizuku/config.hpp"
#include "shizuku/kernel.hpp"
#include "shizuku/object_api.hpp"
#include "shizuku/object_ids.hpp"

namespace car {
namespace id {
constexpr uintptr_t base = shizuku::object_id::count;
constexpr uintptr_t io = base;
constexpr uintptr_t ws2812 = base + 1;
constexpr uintptr_t line_sensor = base + 2;
constexpr uintptr_t motor_fl = base + 3;
constexpr uintptr_t motor_fr = base + 4;
constexpr uintptr_t motor_rl = base + 5;
constexpr uintptr_t motor_rr = base + 6;
constexpr uintptr_t classic_hid = base + 7;
static_assert(classic_hid < shizuku::KERNEL_OBJECT::OBJECT_COUNT,
              "Shizuku object table has no room for the car objects");
}

enum class motor_position : uintptr_t { fl = 1, fr, rl, rr };
enum class motor_method : uintptr_t { main = 0, set_speed = 1 };
enum class pixel_method : uintptr_t { main = 0, set_pixel = 1, set_central = 2, poll = 3 };
enum class line_method : uintptr_t { main = 0, read = 1 };
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

uintptr_t io_main(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
uintptr_t pixel_main(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
uintptr_t line_main(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
uintptr_t motor_main(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
uintptr_t hid_main(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
uintptr_t register_io();
}
#endif
