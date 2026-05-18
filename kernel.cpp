#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <hardware/exception.h>
#include <hardware/irq.h>
#include <kernel.hpp>
#include <pico/stdlib.h>
#include <shizu.hpp>

static_assert(offsetof(shizu::context_t, sp) == 32,
              "context_t layout mismatch: psp offset");
extern "C" {
void svc_asm_handler();
shizu::context_t *get_current_context();
}

void *get_psp();
namespace shizu {
object_t object_table[128];
thread_t thread_table[128];

void object_table_init() {
  for (uint32_t i = 0; i < 128; i++) {
    object_table[i].id = i;
    object_table[i].thread_table.clear();
    object_table[i].state = object_t::state_t::UNINITIALIZED;
  }
}

void create_object(uint32_t obj_num) {
  if (object_table[obj_num].state != object_t::state_t::UNINITIALIZED) {
    panic("object already initialized");
  }
  object_table[obj_num].state = object_t::state_t::KERNEL_SPACE_OBJECT;
}

void thread_table_init() {
  for (size_t i = 0; i < 128; i++) {
    thread_table[i].state = thread_t::state_t::UNINITIALIZED;
    thread_table[i].context = nullptr;
    thread_table[i].object_id = 0;
    thread_table[i].thread_id = i;
  }
}

uint32_t cpu_manager::current_thread_id = 0;

void cpu_manager::init() {
  exception_set_exclusive_handler(SVCALL_EXCEPTION, svc_asm_handler);
}

void thread_t::ready_to_run() {
  this->context = new context_t();
  this->state = state_t::READY;
}

cpu_manager::svc_handler_descriptor cpu_manager::svc_handler_info{nullptr, 0};

namespace FOR_KERNEL_OBJECT {

void create_object(uint32_t obj_num, uint32_t thread_num, method_t entry) {
  shizu::create_object(obj_num);
  shizu::FOR_KERNEL_OBJECT::create_thread(obj_num, thread_num, entry);
}

void create_thread(uint32_t obj_num, uint32_t thread_num, method_t entry) {
  if (thread_table[thread_num].state == thread_t::state_t::UNINITIALIZED) {
    thread_table[thread_num].object_id = obj_num;
    thread_table[thread_num].ready_to_run();
    thread_table[thread_num].context->sp =
        (exception_frame_t *)(((uint32_t)malloc(2048) + 2048 -
                               sizeof(exception_frame_t)) &
                              ~((uint32_t)0xfL));
    thread_table[thread_num].context->sp->pc = (uint32_t)entry;
    thread_table[thread_num].context->sp->lr =
        (uint32_t)shizu::FOR_KERNEL_OBJECT::exit_method;
    thread_table[thread_num].context->sp->r0 = 0;
    thread_table[thread_num].context->sp->r1 = 0;
    thread_table[thread_num].context->sp->r2 = 0;
    thread_table[thread_num].context->sp->r3 = 0;
    thread_table[thread_num].context->sp->r12 = 0;
    thread_table[thread_num].context->sp->xPSR = (1 << 24);
    object_table[obj_num].thread_table.insert(thread_num);
  } else {
    panic("this thread is already initialized\ncalling this system call for "
          "\n thread_id: %d\n object_id: %d\n",
          thread_num, obj_num);
  };
}
void export_method(uint32_t obj_num, uint32_t method_num, method_t entry) {
  shizu::object_table[obj_num].method_table[method_num] = entry;
}
void exit_method(uint32_t return_code) {
  svc<(uint32_t)shizu::kernel_object_svc_num::METHOD_EXIT>(return_code, 0, 0, 0,
                                                           0);
};
} // namespace FOR_KERNEL_OBJECT

} // namespace shizu

shizu::context_t *get_current_context() {
  return shizu::thread_table[shizu::cpu_manager::current_thread_id].context;
}

__always_inline void set_return_code(uint32_t return_code,
                                     shizu::exception_frame_t &context) {
  context.r0 = return_code;
};

void set_return_code_as_syscall_num(shizu::exception_frame_t &stack_frame) {
  stack_frame.r0 = ((*(uint16_t *)(stack_frame.pc - 2)) & 0xff);
}

void trap() {
  while (1) {
    printf("trapped\n");
    sleep_ms(1000);
  }
}

// 本当はcontext_t* svc_cpp_handler(context_t
// *context)として変更の有無に応じてcontextを変更した方が早い
void svc_cpp_handler(shizu::context_t *context) {
  shizu::exception_frame_t *stack_frame = context->sp;
  uint32_t arg0 = stack_frame->r0, arg1 = stack_frame->r1,
           arg2 = stack_frame->r2, arg3 = stack_frame->r3,
           r12 = stack_frame->r12, lr = stack_frame->lr, pc = stack_frame->pc;
  shizu::thread_t &current_thread =
      shizu::thread_table[shizu::cpu_manager::current_thread_id];
  shizu::kernel_object_svc_num svc_num =
      (shizu::kernel_object_svc_num)(*(uint16_t *)(pc - 2) & 0xff);
  /*{

    if (svc_num != shizu::kernel_object_svc_num::GET_THREAD_STATE) {
      // svc_cpp_handler の最初に挿入
      shizu::context_t *ctx = get_current_context();
      uintptr_t ctx_addr = reinterpret_cast<uintptr_t>(ctx);

      // ASM が書き込んでいるオフセットを使って PSP を取得（offset は asm
      // と合わせる）
      uintptr_t saved_psp = 0;
      memcpy(&saved_psp, reinterpret_cast<void *>(ctx_addr + 32),
             sizeof(saved_psp)); // <-- 確認: asm のオフセット32と一致するか

      uintptr_t current_psp;
      asm volatile("mrs %0, PSP" : "=r"(current_psp));

      // ダンプ（printf 使用例）
      printf("DBG_CTX: ctx=%lx ctx_addr=%lx saved_psp=%lx current_psp=%lx "
             "ctx->sp=%lx\n",
             (uint32_t)ctx, (uint32_t)ctx_addr, (uint32_t)saved_psp,
             (uint32_t)current_psp, (uint32_t)ctx->sp);

            uint32_t *stacked = reinterpret_cast<uint32_t *>(saved_psp);
            if (stacked) {
              printf("DBG_STACKED: [%08x %08x %08x %08x]\n", stacked[0],
         stacked[1], stacked[2], stacked[3]);
            }


      uint32_t *stacked = reinterpret_cast<uint32_t *>(ctx->sp);
      uint32_t maybe_svc = stacked[0];
      if (maybe_svc > 1000) { // svc id は小さいはず。閾値は実装で調整
        printf("ctx=%lx stacked=%lx maybe_svc=%lx stacked[1]=%lx\n",
               (uint32_t)ctx, (uint32_t)stacked, maybe_svc, stacked[1]);
      }
    }
  }*/

  if (shizu::object_table[current_thread.object_id].state ==
      shizu::object_t::state_t::KERNEL_OBJECT) {

    shizu::kernel_object_svc_num svc_num =
        (shizu::kernel_object_svc_num)(*(uint16_t *)(pc - 2) & 0xff);

    switch (svc_num) {
    case shizu::kernel_object_svc_num::METHOD_CALL: {
      // printf("call to %lx,%lx\n", arg0, arg1);
      current_thread.call_stack.push(
          {.stack_frame = *stack_frame,
           .context = *context,
           .caller_object_id = current_thread.object_id});
      /*
        current_thread.call_stack.push(
        {.stack_frame = *stack_frame,
         .context = *context,
         .caller_object_id = current_thread.object_id});
      */
      // printf("pc: %lx\n", stack_frame->pc);
      stack_frame->pc = (uint32_t)shizu::object_table[arg0].method_table[arg1];
      stack_frame->lr = (uint32_t)::trap;
      stack_frame->r0 = current_thread.object_id;
      stack_frame->r1 = current_thread.thread_id;
      stack_frame->r2 = arg2;
      stack_frame->r3 = arg3;
      stack_frame->r12 = r12;
      current_thread.object_id = arg0;
      // printf("pc: %lx\n", stack_frame->pc);
      break;
    }
    case shizu::kernel_object_svc_num::METHOD_EXIT: {
      shizu::method_call_stack_t call_stack_top;
      for (size_t i = 0; i < arg1 + 1; i++) {
        if (current_thread.call_stack.empty()) {
          panic("call_stack empty");
        } else {
          call_stack_top = current_thread.call_stack.top();
          current_thread.call_stack.pop();
        }
        *get_current_context() = call_stack_top.context;
        stack_frame = call_stack_top.context.sp;
        *stack_frame = call_stack_top.stack_frame;
        current_thread.object_id = call_stack_top.caller_object_id;
        stack_frame->r0 = 0;
        stack_frame->r1 = arg0;
      }

      break;
    }
    case shizu::kernel_object_svc_num::SET_SVC_HANDLER: {
      shizu::cpu_manager::svc_handler_info.entry_point = (shizu::method_t)arg0;
      shizu::cpu_manager::svc_handler_info.handling_object_id =
          shizu::thread_table[shizu::cpu_manager::current_thread_id].object_id;
      break;
    }

    case shizu::kernel_object_svc_num::GET_CURRENT_THREAD_ID: {
      stack_frame->r1 = shizu::cpu_manager::current_thread_id;
      stack_frame->r0 = 0;
      break;
    }
    case shizu::kernel_object_svc_num::SWITCH_THREAD: {
      if (arg0 == shizu::cpu_manager::current_thread_id) {
        break;
      }
      shizu::thread_t &current_thread =
          shizu::thread_table[shizu::cpu_manager::current_thread_id];
      shizu::thread_t &next_thread = shizu::thread_table[arg0];
      if (current_thread.state == shizu::thread_t::state_t::RUNNING &&
          next_thread.state == shizu::thread_t::state_t::READY) {
        current_thread.state = shizu::thread_t::state_t::READY;
        next_thread.state = shizu::thread_t::state_t::RUNNING;
        shizu::cpu_manager::current_thread_id = arg0;
      }
      break;
    }
    case shizu::kernel_object_svc_num::GET_THREAD_STATE: {
      stack_frame->r1 = (uint32_t)shizu::thread_table[arg0].state;
      stack_frame->r0 = 0;
      break;
    }
    case shizu::kernel_object_svc_num::GET_OBJECT_ID_FROM_THREAD_ID: {
      stack_frame->r1 = (uint32_t)shizu::thread_table[arg0].object_id;
      stack_frame->r0 = 0;
      break;
    }
    default:
      break;
    }
  } else {
    uint32_t caller_obj_num = current_thread.object_id;
    uint32_t caller_thread_id = current_thread.thread_id;
    current_thread.call_stack.push(
        {.stack_frame = *stack_frame,
         .context = *context,
         .caller_object_id = current_thread.object_id});
    stack_frame->pc =
        (uint32_t)shizu::cpu_manager::svc_handler_info.entry_point;
    stack_frame->lr = (uint32_t)shizu::FOR_KERNEL_OBJECT::exit_method;
    current_thread.object_id =
        shizu::cpu_manager::svc_handler_info.handling_object_id;
    current_thread.context->r4 = (uint32_t)svc_num;
    current_thread.context->r5 = caller_obj_num;
    current_thread.context->r6 = caller_thread_id;
  }
}