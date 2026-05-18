#ifndef SHIZU_KERNEL_OBJECT_HPP
#define SHIZU_KERNEL_OBJECT_HPP
#include <kernel.hpp>
#include <svc.hpp>
namespace shizu {
void kernel_object_main();
static inline uint32_t get_current_thread_id() {
  result_t<uint32_t> result =
      svc<(uint32_t)kernel_object_svc_num::GET_CURRENT_THREAD_ID>(0, 0, 0, 0);
  return result.value;
}
}
#endif // SHIZU_KERNEL_OBJECT_HPP