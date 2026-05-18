#ifndef SHIZU_REDIRECT_METHOD_HPP
#define SHIZU_REDIRECT_METHOD_HPP
#include <obj_api.hpp>
#include <object_id.hpp>
void redirect_method(object_ids target_obj_id, uint32_t md_id,
                     object_ids dest_obj_id, uint32_t dest_method_id) {
  shizu::obj_api::svc<255>((uint32_t)target_obj_id, md_id,
                           (uint32_t)dest_obj_id, dest_method_id);
}
#endif // SHIZU_REDIRECT_METHOD_HPP