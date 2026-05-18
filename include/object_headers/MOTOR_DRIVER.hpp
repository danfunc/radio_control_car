#ifndef SHIZU_MOTOR_DRIVER_HPP
#define SHIZU_MOTOR_DRIVER_HPP
#include <cstdint>
namespace shizu
{
    class MOTOR_DRIVER
    {
    public:
        MOTOR_DRIVER(){};
        ~MOTOR_DRIVER(){};
        static void main();
        enum METHOD_IDs: uint32_t{
            init=0,
            set_speed=1,
        };
    };
    
} // namespace shizu

#endif // SHIZU_MOTOR_DRIVER_HPP