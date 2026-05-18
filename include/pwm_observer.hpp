#ifndef SHIZU_PWM_OBSERVER_HPP
#define SHIZU_PWM_OBSERVER_HPP
struct pwm_observer
{
    static void main();
};

enum method_numbers:uint32_t{
    CHANGE_FILTER=0,
    SEND_TO_FILTER=0,
};

#endif // SHIZU_PWM_OBSERVER_HPP