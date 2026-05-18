#ifndef SHIZU_GET_CURRENT_OBJ_ID_HPP
#define SHIZU_GET_CURRENT_OBJ_ID_HPP
#include <obj_api.hpp>
static auto get_current_obj_id() {
  return shizu::obj_api::svc(shizu::obj_api::svc_num::GET_CURRENT_OBJ_ID, 0, 0,
                             0);
}
#endif // SHIZU_GET_CURRENT_OBJ_ID_HPP