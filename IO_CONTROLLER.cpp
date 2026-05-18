#include <call_method.hpp>
#include <hardware/pwm.h>
#include <obj_api.hpp>
#include <object_headers/CYW43_BL_DRIVER.hpp>
#include <object_headers/IO_CONTROLLER.hpp>
#include <object_headers/LINE_SENSOR_DRIVER.hpp>
#include <object_headers/MOTOR_DRIVER.hpp>
#include <object_headers/MOTOR_POSITION.hpp>
#include <object_headers/WS2812_DRIVER.hpp>
#include <object_id.hpp>
#include <pico/stdlib.h>
#include <stdio.h>

void shizu::IO_CONTROLLER::main() {
  printf("IO_CONTROLLER::init\n");
  obj_api::create_object((uint32_t)object_ids::MOTOR_DRIVER_FL, 2,
                         (uintptr_t)MOTOR_DRIVER::main);
  obj_api::create_object((uint32_t)object_ids::MOTOR_DRIVER_FR, 3,
                         (uintptr_t)MOTOR_DRIVER::main);
  obj_api::create_object((uint32_t)object_ids::MOTOR_DRIVER_RL, 4,
                         (uintptr_t)MOTOR_DRIVER::main);
  obj_api::create_object((uint32_t)object_ids::MOTOR_DRIVER_RR, 5,
                         (uintptr_t)MOTOR_DRIVER::main);
  obj_api::create_object((uint32_t)object_ids::LINE_SENSOR_DRIVER, 8,
                         (uintptr_t)LINE_SENSOR_DRIVER::main);
  obj_api::create_object((uint32_t)object_ids::WS2812_DRIVER, 10,
                         (uintptr_t)WS2812_DRIVER::init);

  obj_api::create_object((uint32_t)object_ids::CYW43_BL_DRIVER, 9,
                         (uintptr_t)CYW43_BL_DRIVER::init);
  // PWM有効化

  obj_api::yield_us(1000000);
  call_method(object_ids::MOTOR_DRIVER_FL, MOTOR_DRIVER::METHOD_IDs::init,
              (uint32_t)motor_position::FL);
  call_method(object_ids::MOTOR_DRIVER_FR, MOTOR_DRIVER::METHOD_IDs::init,
              (uint32_t)motor_position::FR);
  call_method(object_ids::MOTOR_DRIVER_RL, MOTOR_DRIVER::METHOD_IDs::init,
              (uint32_t)motor_position::RL);
  call_method(object_ids::MOTOR_DRIVER_RR, MOTOR_DRIVER::METHOD_IDs::init,
              (uint32_t)motor_position::RR);

  // call_method(object_ids::LINE_SENSOR_DRIVER, LINE_SENSOR_DRIVER::get_value,
  // 0);
  while (true) {
    obj_api::yield_us(1000000);
  }
}