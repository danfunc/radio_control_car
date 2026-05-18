#ifndef SHIZU_CALL_METHOD_HPP
#define SHIZU_CALL_METHOD_HPP

#include <obj_api.hpp>
#include <object_id.hpp>

/*shizu::obj_api::svc(shizu::obj_api::svc_num::CALL_METHOD,
                    (uint32_t)object_ids::REDIRECT_CONTROLLER, 0,
                    0);*/
static void call_method(object_ids callee_obj_id, uint32_t callee_method_id,
                        uint32_t arg0) {
  shizu::obj_api::svc(shizu::obj_api::svc_num::CALL_METHOD,
                      (uint32_t)callee_obj_id, callee_method_id, arg0);
}

//   shizu::obj_api::svc(shizu::obj_api::svc_num::CALL_METHOD_VIA_MD,
//                           (uint32_t)object_ids::PWM_OBSERVER_OBJECT, 0, input
//                           - '0');

static void call_method_via_md(uint32_t md, uint32_t arg0, uint32_t arg1 = 0) {
  shizu::obj_api::svc(shizu::obj_api::svc_num::CALL_METHOD_VIA_MD, md, arg0,
                      arg1);
};

#endif // SHIZU_CALL_METHOD_HPP