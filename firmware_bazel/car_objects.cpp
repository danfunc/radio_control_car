#include "car_objects.hpp"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "ws2812.pio.h"

namespace car {
namespace {
using shizuku::object_api;
constexpr uint kPixelPin = 16;
constexpr uint kSensorPins[] = {10, 11, 12};
struct motor_state { uint fw; uint bw; bool ready; };
motor_state motors[4]{};
uint32_t pixels[8]{};

motor_state *current_motor() {
  const auto who = api(object_api::GET_CURRENT_OBJECT);
  if (who.error != 0 || who.value < id::motor_fl || who.value > id::motor_rr)
    return nullptr;
  return &motors[who.value - id::motor_fl];
}

uintptr_t motor_set_speed(uintptr_t raw, uintptr_t, uintptr_t, uintptr_t) {
  auto *motor = current_motor();
  if (!motor || !motor->ready) return 1;
  int32_t duty = static_cast<int32_t>(raw > 2000 ? 2000 : raw) - 1000;
  const uint slice = pwm_gpio_to_slice_num(motor->fw);
  const uint16_t level = static_cast<uint16_t>((duty < 0 ? -duty : duty) * 25 / 4);
  pwm_set_chan_level(slice, pwm_gpio_to_channel(motor->fw), duty > 0 ? level : 0);
  pwm_set_chan_level(slice, pwm_gpio_to_channel(motor->bw), duty < 0 ? level : 0);

  const auto who = api(object_api::GET_CURRENT_OBJECT);
  // LED order: FFR CFR CRR RRR RRL CRL CFL FFL.
  constexpr uint8_t led_positions[] = {7, 0, 4, 3};
  const uintptr_t led = led_positions[who.value - id::motor_fl];
  const uint32_t red = duty < 0 ? (static_cast<uint32_t>(-duty) * 255 / 1000) : 0;
  const uint32_t green = duty > 0 ? (static_cast<uint32_t>(duty) * 255 / 1000) : 0;
  const uint32_t grb = (green << 16) | (red << 8);
  return call(id::ws2812, (uintptr_t)pixel_method::set_pixel,
              pixel_arg(led, grb)).error;
}

uintptr_t pixel_set(uintptr_t packed, uintptr_t, uintptr_t, uintptr_t) {
  const uintptr_t position = packed >> 24;
  if (position >= 8) return 1;
  pixels[position] = static_cast<uint32_t>(packed & 0xFFFFFFu);
  return 0;
}
uintptr_t pixel_central(uintptr_t grb, uintptr_t, uintptr_t, uintptr_t) {
  constexpr uint positions[] = {1, 2, 5, 6};
  for (const uint position : positions) pixels[position] = grb & 0xFFFFFFu;
  return 0;
}
uintptr_t pixel_poll(uintptr_t, uintptr_t, uintptr_t, uintptr_t) {
  PIO pio;
  uint sm, offset;
  if (!pio_claim_free_sm_and_add_program_for_gpio_range(
          &ws2812_program, &pio, &sm, &offset, kPixelPin, 1, true)) {
    shizuku::KERNEL::BOARD::diag_printf("[CAR] WS2812 PIO allocation failed\n");
    return 1;
  }
  ws2812_program_init(pio, sm, offset, kPixelPin, 800000, false);
  while (true) {
    for (uint i = 0; i < 8; ++i)
      pio_sm_put_blocking(pio, sm, pixels[i] << 8);
    api(object_api::SLEEP_US, 10000);
  }
}

uintptr_t line_read(uintptr_t, uintptr_t, uintptr_t, uintptr_t) {
  return (gpio_get(kSensorPins[0]) << 2) |
         (gpio_get(kSensorPins[1]) << 1) | gpio_get(kSensorPins[2]);
}

bool create_and_init(uintptr_t id, uintptr_t entry, uintptr_t flags,
                     uintptr_t argument = 0) {
  const auto created = api(object_api::CREATE_OBJECT, id, entry, flags);
  if (created.error != 0) return false;
  const auto init = call(id, 0, argument);
  return init.error == 0 && init.value == 0;
}
} // namespace

uintptr_t motor_main(uintptr_t position, uintptr_t, uintptr_t, uintptr_t) {
  auto *motor = current_motor();
  if (!motor) return 1;
  switch (static_cast<motor_position>(position)) {
  case motor_position::fl: motor->fw = 19; motor->bw = 18; break;
  case motor_position::fr: motor->fw = 8; motor->bw = 9; break;
  case motor_position::rl: motor->fw = 20; motor->bw = 21; break;
  case motor_position::rr: motor->fw = 6; motor->bw = 7; break;
  default: return 1;
  }
  gpio_set_function(motor->fw, GPIO_FUNC_PWM);
  gpio_set_function(motor->bw, GPIO_FUNC_PWM);
  const uint slice = pwm_gpio_to_slice_num(motor->fw);
  pwm_set_clkdiv(slice, 20.0f);
  pwm_set_wrap(slice, 6249);
  pwm_set_chan_level(slice, pwm_gpio_to_channel(motor->fw), 0);
  pwm_set_chan_level(slice, pwm_gpio_to_channel(motor->bw), 0);
  pwm_set_enabled(slice, true);
  motor->ready = true;
  return api(object_api::EXPORT_METHOD, (uintptr_t)motor_method::set_speed,
             (uintptr_t)&motor_set_speed).error;
}

uintptr_t line_main(uintptr_t, uintptr_t, uintptr_t, uintptr_t) {
  for (const uint pin : kSensorPins) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
  }
  return api(object_api::EXPORT_METHOD, (uintptr_t)line_method::read,
             (uintptr_t)&line_read).error;
}

uintptr_t pixel_main(uintptr_t, uintptr_t, uintptr_t, uintptr_t) {
  uintptr_t failures = 0;
  failures += api(object_api::EXPORT_METHOD, (uintptr_t)pixel_method::set_pixel,
                  (uintptr_t)&pixel_set).error;
  failures += api(object_api::EXPORT_METHOD, (uintptr_t)pixel_method::set_central,
                  (uintptr_t)&pixel_central).error;
  failures += api(object_api::EXPORT_METHOD, (uintptr_t)pixel_method::poll,
                  (uintptr_t)&pixel_poll).error;
  return failures;
}

uintptr_t io_main(uintptr_t, uintptr_t, uintptr_t, uintptr_t) {
  constexpr uintptr_t peripheral_flags =
      shizuku::OBJECT_PRIVILEGED | shizuku::OBJECT_ON_CORE(0);
  uintptr_t failures = 0;
  failures += !create_and_init(id::ws2812, (uintptr_t)&pixel_main, peripheral_flags);
  failures += !create_and_init(id::line_sensor, (uintptr_t)&line_main, peripheral_flags);
  failures += !create_and_init(id::motor_fl, (uintptr_t)&motor_main, peripheral_flags,
                               (uintptr_t)motor_position::fl);
  failures += !create_and_init(id::motor_fr, (uintptr_t)&motor_main, peripheral_flags,
                               (uintptr_t)motor_position::fr);
  failures += !create_and_init(id::motor_rl, (uintptr_t)&motor_main, peripheral_flags,
                               (uintptr_t)motor_position::rl);
  failures += !create_and_init(id::motor_rr, (uintptr_t)&motor_main, peripheral_flags,
                               (uintptr_t)motor_position::rr);
  failures += !create_and_init(id::classic_hid, (uintptr_t)&hid_main, peripheral_flags);
  if (failures != 0) return failures;
  failures += api(object_api::SPAWN, id::ws2812, (uintptr_t)pixel_method::poll).error != 0;
  failures += api(object_api::SPAWN, id::classic_hid, (uintptr_t)hid_method::poll).error != 0;
  return failures;
}

uintptr_t register_io() {
  if (!create_and_init(id::io, (uintptr_t)&io_main,
                       shizuku::OBJECT_PRIVILEGED | shizuku::OBJECT_ON_CORE(0)))
    return 1;
  return 0;
}
} // namespace car
