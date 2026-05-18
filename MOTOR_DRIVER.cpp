#include <call_method.hpp>
#include <cmath>
#include <export_method.hpp>
#include <hardware/pwm.h>
#include <obj_api.hpp>
#include <object_headers/MOTOR_DRIVER.hpp>
#include <object_headers/MOTOR_POSITION.hpp>
#include <object_headers/WS2812_DRIVER.hpp>
#include <object_id.hpp>
#include <pico/stdlib.h>
#include <stdio.h>
static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint32_t)(r) << 8) | ((uint32_t)(g) << 16) | (uint32_t)(b);
}

struct motor_pins {
  uint32_t fw;
  uint32_t bw;
};

static motor_pins get_pins(shizu::motor_position position) {
  switch (position) {
  case shizu::motor_position::FL:
    return {19, 18};
  case shizu::motor_position::RL:
    return {20, 21};
  case shizu::motor_position::RR:
    return {6, 7};
  case shizu::motor_position::FR:
    return {8, 9};
  default:
    return {0, 0};
  }
}

static WS2812_DRIVER::LED_POSITION
get_led_position(shizu::motor_position position) {
  switch (position) {
  case shizu::motor_position::FL:
    return WS2812_DRIVER::LED_POSITION::FFL;
  case shizu::motor_position::FR:
    return WS2812_DRIVER::LED_POSITION::FFR;
  case shizu::motor_position::RL:
    return WS2812_DRIVER::LED_POSITION::RRL;
  case shizu::motor_position::RR:
    return WS2812_DRIVER::LED_POSITION::RRR;
  default:
    return WS2812_DRIVER::LED_POSITION::FFL;
  }
}

static void init_method(uint32_t caller_obj_id, uint32_t caller_thread_id,
                        uint32_t position_raw, uint32_t _arg1) {

  shizu::motor_position position = (shizu::motor_position)position_raw;
  shizu::obj_api::set_memory(0, position_raw);
  shizu::obj_api::set_memory(1, 0); // speed

  motor_pins pins = get_pins(position);
  if (pins.fw == 0) {
    return;
  }

  {
    // init gpio
    gpio_set_function(pins.fw, GPIO_FUNC_PWM);
    gpio_set_function(pins.bw, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(pins.fw);
    pwm_set_clkdiv(slice_num, 20.0f);
    pwm_set_wrap(slice_num, 6249); // 1kHz (125MHz / 20 / 6250)
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(pins.fw), 0);
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(pins.bw), 0);
    pwm_set_enabled(slice_num, true);
  }
  // printf("init_method called\ncaller_obj_id: %lu\ncaller_thread_id: %lu\n",
  //       caller_obj_id, caller_thread_id);
  shizu::obj_api::svc(shizu::obj_api::svc_num::EXIT_METHOD, 0, 0, 0, 0);
};

static void set_speed_method(uint32_t caller_obj_id, uint32_t caller_thread_id,
                             uint32_t speed, uint32_t _arg1) {
  // printf("set_speed_method called\ncaller_obj_id: %ld\ncaller_thread_id:
  // %ld\n",
  //     caller_obj_id, caller_thread_id);
  auto res = shizu::obj_api::get_memory(0);
  shizu::motor_position position = (shizu::motor_position)res.value;

  motor_pins pins = get_pins(position);
  if (pins.fw == 0) {
    return;
  }
  uint slice_num = pwm_gpio_to_slice_num(pins.fw);
  int32_t duty = (int32_t)speed - 1000;
  if (duty > 1000)
    duty = 1000;
  if (duty < -1000)
    duty = -1000;
  shizu::obj_api::set_memory(1, (uint32_t)duty);

  uint16_t level = (uint16_t)((std::abs(duty) * 25) / 4);
  if (duty > 0) {
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(pins.fw), level);
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(pins.bw), 0);
  } else {
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(pins.fw), 0);
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(pins.bw), level);
  }

  // LED色を設定
  WS2812_DRIVER::LED_POSITION led_pos = get_led_position(position);
  call_method(::object_ids::WS2812_DRIVER,
              WS2812_DRIVER::METHOD_IDs::select_position, (uint32_t)led_pos);

  uint8_t r = duty < 0 ? (uint8_t)(-duty / 1000 * 255) : 0;
  uint8_t g = duty > 0 ? (uint8_t)(duty / 1000 * 255) : 0;
  uint32_t color = urgb_u32(r, g, 0);
  call_method(::object_ids::WS2812_DRIVER, WS2812_DRIVER::METHOD_IDs::set_color,
              color);
}

namespace shizu {
void MOTOR_DRIVER::main() {
  printf("MOTOR_DRIVER::init\n");
  export_method<init_method>(METHOD_IDs::init);
  export_method<set_speed_method>(METHOD_IDs::set_speed);
  obj_api::yield_us(1000000 * 1.5);
  while (1) {
    obj_api::yield_us(10000);
  }
}
} // namespace shizu