#ifndef SHIZU_MOTOR_POSITION_HPP
#define SHIZU_MOTOR_POSITION_HPP
#include <cstdint>
namespace shizu {
enum struct motor_position : uint32_t {
  FL = 1,
  FR = 2,
  RL = 3,
  RR = 4,
};
};
#endif // SHIZU_MOTOR_POSITION_HPP