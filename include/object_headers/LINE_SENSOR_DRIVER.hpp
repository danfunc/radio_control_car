#ifndef SHIZU_LINE_SENSOR_DRIVER_HPP
#define SHIZU_LINE_SENSOR_DRIVER_HPP
#include <cstdint>
namespace shizu {
class LINE_SENSOR_DRIVER {
public:
  LINE_SENSOR_DRIVER() {};
  ~LINE_SENSOR_DRIVER() {};
  static void main();
  enum METHOD_IDs : uint32_t {
    get_value = 0,
  };
};
} // namespace shizu

#endif // SHIZU_LINE_SENSOR_DRIVER_HPP