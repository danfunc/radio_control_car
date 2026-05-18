#include <export_method.hpp>
#include <obj_api.hpp>
#include <object_headers/WS2812_DRIVER.hpp>
#include <ws2812.pio.h>

// --- Neopixel ---
const uint WS2812_PIN = 16;
const PIO PIO_NEOPIXEL = pio0;
const uint SM_NEOPIXEL = 0;

static inline void put_pixel(uint32_t pixel_grb) {
  for (size_t i = 0; i < 8; i++) {
    pio_sm_put_blocking(PIO_NEOPIXEL, SM_NEOPIXEL, pixel_grb << 8u);
  }
}

static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint32_t)(r) << 8) | ((uint32_t)(g) << 16) | (uint32_t)(b);
}

static void set_color(uint32_t _caller_obj_id, uint32_t _caller_thread_id,
                      uint32_t color, uint32_t arg1) {
  uint32_t led_position = shizu::obj_api::get_memory(9).value;
  shizu::obj_api::set_memory(led_position, color);
}

static void select_led(uint32_t _caller_obj_id, uint32_t _caller_thread_id,
                       uint32_t led_position, uint32_t arg1) {
  shizu::obj_api::set_memory(9, led_position);
}

static void set_central_color(uint32_t _caller_obj_id,
                              uint32_t _caller_thread_id, uint32_t color,
                              uint32_t _arg1) {
  shizu::obj_api::set_memory(::WS2812_DRIVER::LED_POSITION::CFL, color);
  shizu::obj_api::set_memory(::WS2812_DRIVER::LED_POSITION::CFR, color);
  shizu::obj_api::set_memory(::WS2812_DRIVER::LED_POSITION::CRL, color);
  shizu::obj_api::set_memory(::WS2812_DRIVER::LED_POSITION::CRR, color);
}

void WS2812_DRIVER::init() {
  export_method<::select_led>(WS2812_DRIVER::METHOD_IDs::select_position);
  export_method<::set_color>(WS2812_DRIVER::METHOD_IDs::set_color);
  export_method<::set_central_color>(
      WS2812_DRIVER::METHOD_IDs::set_central_color);
  uint offset = pio_add_program(PIO_NEOPIXEL, &ws2812_program);
  ws2812_program_init(PIO_NEOPIXEL, SM_NEOPIXEL, offset, WS2812_PIN, 800000,
                      false);
  for (size_t i = 0; i < 8; i++) {
    shizu::obj_api::set_memory(i, 0);
  }
  put_pixel(urgb_u32(0, 0, 0));
  while (1) {
    shizu::obj_api::yield_us(10000);
    for (size_t i = 0; i < 8; i++) {
      uint32_t pixel_grb = shizu::obj_api::get_memory(i).value;
      pio_sm_put_blocking(PIO_NEOPIXEL, SM_NEOPIXEL, pixel_grb << 8u);
    }
  }
}