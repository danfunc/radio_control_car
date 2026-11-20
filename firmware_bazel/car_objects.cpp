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
struct motor_state { uint fw; uint bw; };
// FL FR RL RR
constexpr motor_state kMotorPins[4] = {{19, 18}, {8, 9}, {20, 21}, {6, 7}};
// LED order: FFR CFR CRR RRR RRL CRL CFL FFL.
constexpr uint8_t kMotorLed[4] = {7, 0, 4, 3};
uint32_t pixels[8]{};
bool motors_ready = false;

uintptr_t set_pixel_raw(uintptr_t packed) {
  const uintptr_t position = packed >> 24;
  if (position >= 8) return 1;
  pixels[position] = static_cast<uint32_t>(packed & 0xFFFFFFu);
  return 0;
}

uintptr_t io_set_motor(uintptr_t packed, uintptr_t, uintptr_t, uintptr_t) {
  const uintptr_t position = packed >> 16;
  const uintptr_t raw = packed & 0xFFFFu;
  if (!motors_ready || position >= 4) return 1;
  const motor_state &motor = kMotorPins[position];
  const int32_t duty = static_cast<int32_t>(raw > 2000 ? 2000 : raw) - 1000;
  const uint slice = pwm_gpio_to_slice_num(motor.fw);
  const uint16_t level = static_cast<uint16_t>((duty < 0 ? -duty : duty) * 25 / 4);
  pwm_set_chan_level(slice, pwm_gpio_to_channel(motor.fw), duty > 0 ? level : 0);
  pwm_set_chan_level(slice, pwm_gpio_to_channel(motor.bw), duty < 0 ? level : 0);

  const uint32_t red = duty < 0 ? (static_cast<uint32_t>(-duty) * 255 / 1000) : 0;
  const uint32_t green = duty > 0 ? (static_cast<uint32_t>(duty) * 255 / 1000) : 0;
  return set_pixel_raw(pixel_arg(kMotorLed[position], (green << 16) | (red << 8)));
}

uintptr_t io_set_pixel(uintptr_t packed, uintptr_t, uintptr_t, uintptr_t) {
  return set_pixel_raw(packed);
}
uintptr_t io_set_central(uintptr_t grb, uintptr_t, uintptr_t, uintptr_t) {
  constexpr uint positions[] = {1, 2, 5, 6};
  for (const uint position : positions) pixels[position] = grb & 0xFFFFFFu;
  return 0;
}
uintptr_t io_poll(uintptr_t, uintptr_t, uintptr_t, uintptr_t) {
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
uintptr_t io_line_read(uintptr_t, uintptr_t, uintptr_t, uintptr_t) {
  return (gpio_get(kSensorPins[0]) << 2) |
         (gpio_get(kSensorPins[1]) << 1) | gpio_get(kSensorPins[2]);
}

bool create_and_init(uintptr_t id, uintptr_t entry, uintptr_t flags) {
  const auto created = api(object_api::CREATE_OBJECT, id, entry, flags);
  if (created.error != 0) return false;
  const auto init = call(id, 0, 0);
  return init.error == 0 && init.value == 0;
}
} // namespace

uintptr_t io_main(uintptr_t, uintptr_t, uintptr_t, uintptr_t) {
  for (const uint pin : kSensorPins) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
  }
  for (const motor_state &motor : kMotorPins) {
    gpio_set_function(motor.fw, GPIO_FUNC_PWM);
    gpio_set_function(motor.bw, GPIO_FUNC_PWM);
    const uint slice = pwm_gpio_to_slice_num(motor.fw);
    pwm_set_clkdiv(slice, 20.0f);
    pwm_set_wrap(slice, 6249);
    pwm_set_chan_level(slice, pwm_gpio_to_channel(motor.fw), 0);
    pwm_set_chan_level(slice, pwm_gpio_to_channel(motor.bw), 0);
    pwm_set_enabled(slice, true);
  }
  motors_ready = true;
  struct entry { io_method m; uintptr_t (*fn)(uintptr_t, uintptr_t, uintptr_t, uintptr_t); };
  constexpr entry entries[] = {
      {io_method::set_motor, &io_set_motor}, {io_method::set_pixel, &io_set_pixel},
      {io_method::set_central, &io_set_central}, {io_method::poll, &io_poll},
      {io_method::line_read, &io_line_read}};
  uintptr_t failures = 0;
  for (const entry &e : entries)
    failures += api(object_api::EXPORT_METHOD, (uintptr_t)e.m, (uintptr_t)e.fn).error;
  return failures;
}

uintptr_t register_objects() {
  constexpr uintptr_t flags = shizuku::OBJECT_PRIVILEGED | shizuku::OBJECT_ON_CORE(0);
  uintptr_t failures = 0;
  failures += !create_and_init(id::io, (uintptr_t)&io_main, flags);
  failures += !create_and_init(id::classic_hid, (uintptr_t)&hid_main, flags);
  if (failures == 0)
    failures += api(object_api::SPAWN, id::io, (uintptr_t)io_method::poll).error != 0;
  return failures;
}
} // namespace car
