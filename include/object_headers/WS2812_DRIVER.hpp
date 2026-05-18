#ifndef SHIZU_WS2812_DRIVER_HPP
#define SHIZU_WS2812_DRIVER_HPP
#include <cstdint>

class WS2812_DRIVER {
private:
  /* data */
public:
  WS2812_DRIVER() {};
  ~WS2812_DRIVER() {};
  static void init();
  enum METHOD_IDs : uint32_t {
    set_color = 0,
    set_central_color = 1,
    select_position=2,
  };
  enum LED_POSITION : uint32_t {
    FFR = 0,
    CFR = 1,
    CRR = 2,
    RRR = 3,
    RRL = 4,
    CRL = 5,
    CFL = 6,
    FFL = 7,
  };
  enum COMMON_COLOR : uint32_t {
    RED = 0,
    GREEN = 1,
    BLUE = 2,
  };
};

#endif // SHIZU_WS2812_DRIVER_HPP