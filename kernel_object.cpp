
#include <cstdio>
#include <cstdlib>
#include <kernel_object.hpp>
#include <map>
#include <obj_api.hpp>
#include <object_headers/IO_CONTROLLER.hpp>
#include <object_id.hpp>
#include <pico/stdlib.h>
#include <shizu.hpp>
#include <svc.hpp>

namespace shizu {

namespace obj_api {} // namespace obj_api

void exported_method() {
  printf("exported_method called\n");
  // for_userland::exit_method(54);
}
void thread_main() {
  while (1) {
    printf("thread_switched\n");
    sleep_ms(500);
    printf("return_to_boot_thread\n");
    svc<(uint32_t)shizu::kernel_object_svc_num::SWITCH_THREAD>(0, 0, 0, 0);
  }
}

void worker_thread1() {
  while (1) {
    printf("worker_thread1\n");
    sleep_ms(500);
    printf("switch_to_worker2\n");
    svc<(uint32_t)shizu::kernel_object_svc_num::SWITCH_THREAD>(2, 0, 0, 0);
  }
}
void worker_thread2() {
  while (1) {
    printf("worker_thread2\n");
    sleep_ms(500);
    printf("switch_to_app_object\n");
    svc<(uint32_t)shizu::kernel_object_svc_num::SWITCH_THREAD>(3, 0, 0, 0);
  }
}

struct method_descriptor_t {
  uint32_t object_id;
  uint32_t method_id;
};

std::map<uint32_t, std::map<uint32_t, method_descriptor_t>> method_map;
std::map<uint32_t, std::map<uint32_t, uint32_t>> memory_map;

uint32_t kernel_obj_svc_handler(uint32_t r0, uint32_t r1, uint32_t r2,
                                uint32_t r3, uint32_t r4, uint32_t r5,
                                uint32_t r6, uint32_t r12) {
  // printf("KERNEL_OBJ_SVC_HANDLER_ENTRY: r0=%lx, r1=%lx, r2=%lx, "
  //       "r3=%lx,r4=%lx,r5=%lx,r6=%lx\n",
  //       r0, r1, r2, r3, r4, r5, r6);
  // 以下は臨時のset_md(4)
  if (r4 == 100) {
    svc<(uint32_t)shizu::kernel_object_svc_num::METHOD_EXIT>(0, 0, 0, 0, 0);
  }
  if (r4 == 255) {
    method_map[r0][r1] = {r2, r3}; // 要改修
    svc<(uint32_t)shizu::kernel_object_svc_num::METHOD_EXIT>(0, 0, 0, 0, 0);
  }

  shizu::obj_api::svc_num svc_num = (shizu::obj_api::svc_num)r0;
  switch (svc_num) {
  case shizu::obj_api::svc_num::CREATE_OBJECT: {
    shizu::FOR_KERNEL_OBJECT::create_object(r1, r2, (shizu::method_t)r3);
    break;
  }
  case shizu::obj_api::svc_num::CREATE_THREAD: {
    shizu::FOR_KERNEL_OBJECT::create_thread(r1, r2, (shizu::method_t)r3);
    break;
  }
  case shizu::obj_api::svc_num::EXPORT_METHOD: {
    // printf("EXPORT_METHOD\n");
    // r1: method_num, r2: entry
    shizu::FOR_KERNEL_OBJECT::export_method(r5, r1, (shizu::method_t)r2);
    break;
  }
  case shizu::obj_api::svc_num::EXIT_METHOD: {
    svc<(uint32_t)shizu::kernel_object_svc_num::METHOD_EXIT>(r1, 1, 0, 0, 0);
    break;
  }
  case shizu::obj_api::svc_num::YIELD: {

    ::result_t<uint32_t> current_id_res =
        ::svc<(uint32_t)shizu::kernel_object_svc_num::GET_CURRENT_THREAD_ID>(
            0, 0, 0, 0);
    const uint32_t current_id = current_id_res.value;
    uint32_t next_id = (current_id + 1) % 128;
    while (next_id != current_id) {
      ::result_t<uint32_t> state_res =
          ::svc<(uint32_t)shizu::kernel_object_svc_num::GET_THREAD_STATE>(
              next_id, 0, 0, 0);
      if (state_res.value == (uint32_t)shizu::thread_t::state_t::READY) {
        break;
      }
      next_id = (next_id + 1) % 128;
    }
    // printf("jump to %d \n", next_id);
    if (next_id != current_id) {
      ::svc<(uint32_t)shizu::kernel_object_svc_num::SWITCH_THREAD>(next_id, 0,
                                                                   0, 0);
    }
    break;
  }
  case shizu::obj_api::svc_num::CALL_METHOD: {
    ::svc<(uint32_t)shizu::kernel_object_svc_num::METHOD_CALL>(r1, r2, r3, 0);
    break;
  }
  case shizu::obj_api::svc_num::CALL_METHOD_VIA_MD: {
    ::svc<(uint32_t)shizu::kernel_object_svc_num::METHOD_CALL>(
        method_map[r1][r2].object_id, method_map[r1][r2].method_id, r3, 0);
    break;
  }
  case shizu::obj_api::svc_num::SET_OBJECT_MD: {

    method_map[r1][r2] = {r3, 0}; // 要改修
    break;
  }
  case shizu::obj_api::svc_num::GET_CURRENT_OBJ_ID: {
    svc<(uint32_t)shizu::kernel_object_svc_num::METHOD_EXIT>(r5, 0, 0, 0, 0);
    break;
  }
  case shizu::obj_api::svc_num::SET_OBJ_MEMORY: {
    memory_map[r5][r1] = r2;
    break;
  }
  case shizu::obj_api::svc_num::GET_OBJ_MEMORY: {
    svc<(uint32_t)shizu::kernel_object_svc_num::METHOD_EXIT>(memory_map[r5][r1],
                                                             0, 0, 0, 0);
    break;
  }
  default: {
    printf("undefined obj_svc_num\n"
           "called_obj_svc_num: %lx\n",
           r0);
    break;
  }
  }

  svc<(uint32_t)shizu::kernel_object_svc_num::METHOD_EXIT>(0, 0, 0, 0, 0);
  return 0;
}

template <auto T>
  requires std::invocable<decltype(T), uint32_t, uint32_t, uint32_t, uint32_t,
                          uint32_t, uint32_t, uint32_t, uint32_t>
__attribute__((naked, aligned(4))) void wrapper() {
  asm volatile("push {r4-r7,r12, lr}\n"
               "ldr  r4, 1f\n"
               "blx  r4\n"
               "pop  {r4-r7,r12, pc}\n"
               ".align 2\n"
               "1: .word %c0\n"
               :
               : "i"(reinterpret_cast<uint32_t>(T))
               :);
}

void kernel_object_main() {
  method_map = {};
  memory_map = {};
  svc<(uint32_t)shizu::kernel_object_svc_num::SET_SVC_HANDLER>(
      (uint32_t)(&shizu::wrapper<kernel_obj_svc_handler>), 0, 0, 0, 0);
  FOR_KERNEL_OBJECT::create_object((uint32_t)object_ids::IO_CONTROLLER, 1,
                                   (method_t)IO_CONTROLLER::main);
  svc<(uint32_t)shizu::kernel_object_svc_num::SWITCH_THREAD>(1, 0, 0, 0);
  while (1) {
    ::svc<(uint32_t)shizu::kernel_object_svc_num::SWITCH_THREAD>(1, 0, 0, 0);
  }

  while (1) {
    const uint32_t current_id = 0;
    uint32_t next_id = (current_id + 1) % 128;

    while (next_id != current_id) {
      ::result_t<uint32_t> state_res =
          ::svc<(uint32_t)shizu::kernel_object_svc_num::GET_THREAD_STATE>(
              next_id, 0, 0, 0);
      if (state_res.value == (uint32_t)shizu::thread_t::state_t::READY) {
        break;
      }
      next_id = (next_id + 1) % 128;
    }
    if (next_id != current_id) {
      ::svc<(uint32_t)shizu::kernel_object_svc_num::SWITCH_THREAD>(next_id, 0,
                                                                   0, 0);
    }
  }

  while (1) {
    printf("kernel_object_main\n");
    printf("svc_calling\n");
    // result_t<uint32_t> result =
    // svc<(uint32_t)shizu::kernel_object_svc_num::METHOD_CALL>(0, 0, 0, 0, 0);

    // printf("svc_called\nreturn: %lx\n", result.result);

    sleep_ms(500);
  }
}
} // namespace shizu