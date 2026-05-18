#ifndef SHIZU_SVC_HPP
#define SHIZU_SVC_HPP

#include <cstdint>

template <typename T>
  requires(sizeof(T) <= 4)
struct result_t {
  enum struct state_t : uint32_t { SUCCESS = 0, FAILURE } result;
  uint32_t value;
  T get_value() { return static_cast<T>(value); }
};

template <uintptr_t sys_call_num>
  requires(sys_call_num <= 255) // SVC instruction immediate must be under 256.
static result_t<uint32_t> svc(uintptr_t arg0, uintptr_t arg1, uintptr_t arg2,
                              uintptr_t arg3, uintptr_t r12 = 0) {
  uint32_t return_code, value;
  asm volatile(
      "mov r0,%[arg0];"
      "mov r1,%[arg1];"
      "mov r2,%[arg2];"
      "mov r3,%[arg3];"
      "mov r12,%[r12];"
      "svc %[sys_call_num];"
      "mov %[return_code],r0;"
      "mov %[value],r1"
      : [return_code] "=r"(return_code), [value] "=r"(value)
      : [arg0] "r"(arg0), [arg1] "r"(arg1), [arg2] "r"(arg2), [arg3] "r"(arg3),
        [r12] "r"(r12), [sys_call_num] "i"(sys_call_num)
      : "r0", "r1", "r2", "r3", "ip", "lr", "memory");
  return {.result = (result_t<uint32_t>::state_t)return_code, .value = value};
};

#endif // SHIZU_SVC_HPP