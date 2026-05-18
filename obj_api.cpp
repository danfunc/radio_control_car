#include <cstdint>
#include <obj_api.hpp>

namespace shizu {
namespace obj_api {

result_t<uint32_t> create_object(uint32_t obj_num, uint32_t thread_num,
                                 uintptr_t entry) {
  return svc(svc_num::CREATE_OBJECT, (uintptr_t)obj_num, (uintptr_t)thread_num,
             (uintptr_t)entry);
}

result_t<uint32_t> export_method(uint32_t method_num, uintptr_t entry) {
  return svc(svc_num::EXPORT_METHOD, (uintptr_t)method_num, (uintptr_t)entry,
             0);
}

result_t<uint32_t> set_memory(uint32_t memory_num, uint32_t value) {
  return svc(svc_num::SET_OBJ_MEMORY, (uintptr_t)memory_num, (uintptr_t)value,
             0);
}

result_t<uint32_t> get_memory(uint32_t memory_num) {
  return svc(svc_num::GET_OBJ_MEMORY, (uintptr_t)memory_num, 0, 0);
}

result_t<uint32_t> set_object_md(uint32_t md_id, uint32_t target_obj_id,
                                 uint32_t target_method_id) {
  return svc(svc_num::SET_OBJECT_MD, (uintptr_t)md_id, (uintptr_t)target_obj_id,
             (uintptr_t)target_method_id);
}
} // namespace obj_api
} // namespace shizu
