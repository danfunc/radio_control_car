#include <export_method.hpp>
#include <hardware/gpio.h>
#include <obj_api.hpp>
#include <object_headers/LINE_SENSOR_DRIVER.hpp>
#include <pico/stdlib.h>
#include <stdio.h>

// Pin definitions
// Adjust these pins according to your actual hardware connection
static const uint PIN_TRACKING_LEFT = 10;
static const uint PIN_TRACKING_CENTER = 11;
static const uint PIN_TRACKING_RIGHT = 12;

static uint32_t get_value_method(uint32_t caller_obj_id,
                                 uint32_t caller_thread_id, uint32_t arg0,
                                 uint32_t arg1) {
  uint32_t val0 = gpio_get(PIN_TRACKING_LEFT) ? 1 : 0;
  uint32_t val1 = gpio_get(PIN_TRACKING_CENTER) ? 1 : 0;
  uint32_t val2 = gpio_get(PIN_TRACKING_RIGHT) ? 1 : 0;

  // sensorValue[3] = sensorValue[0] << 2 | sensorValue[1] << 1 |
  // sensorValue[2];
  uint32_t result = (val0 << 2) | (val1 << 1) | val2;
  printf("get_value_method called\ncaller_obj_id: %ld\ncaller_thread_id: "
         "%ld\nsensor_value: %ld\n",
         caller_obj_id, caller_thread_id, result);
  shizu::obj_api::svc(shizu::obj_api::svc_num::EXIT_METHOD, result, 0, 0, 0);
  return result;
}

namespace shizu {
void LINE_SENSOR_DRIVER::main() {
  printf("LINE_SENSOR_DRIVER::init\n");
  // Initialize GPIOs
  gpio_init(PIN_TRACKING_LEFT);
  gpio_set_dir(PIN_TRACKING_LEFT, GPIO_IN);

  gpio_init(PIN_TRACKING_CENTER);
  gpio_set_dir(PIN_TRACKING_CENTER, GPIO_IN);

  gpio_init(PIN_TRACKING_RIGHT);
  gpio_set_dir(PIN_TRACKING_RIGHT, GPIO_IN);

  export_method<get_value_method>(METHOD_IDs::get_value);

  while (true) {
    obj_api::yield_us(1000000);
  }
}
} // namespace shizu