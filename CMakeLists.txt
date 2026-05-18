cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 20)

# Pico SDKの機能を利用したボード設定のデフォルト値
# ※CYW43(無線)を含んでいるため、実機に合わせて pico_w や pico2_w に適宜変更してください。
if (NOT PICO_BOARD)
    set(PICO_BOARD pico2_w CACHE STRING "Board type")
endif()

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(shizupico C CXX ASM)
pico_sdk_init()
add_executable(main main.cpp kernel.cpp kernel_object.cpp svc_asm_handler.S obj_api.cpp)
target_sources(main PRIVATE IO_CONTROLLER.cpp MOTOR_DRIVER.cpp LINE_SENSOR_DRIVER.cpp CYW43_BL_DRIVER.cpp WS2812_DRIVER.cpp)
target_link_libraries(main pico_stdlib pico_stdio hardware_exception hardware_irq hardware_pwm hardware_gpio hardware_pio pico_btstack_classic pico_cyw43_arch_poll pico_btstack_cyw43 pico_lwip_nosys)
target_include_directories(main PRIVATE include)
pico_generate_pio_header(main ${CMAKE_CURRENT_SOURCE_DIR}/ws2812.pio)
pico_add_extra_outputs(main)
pico_enable_stdio_usb(main 1)
target_compile_options(main PRIVATE -mfloat-abi=soft -Wall)



# 例：プロコンのMACアドレスが98:B6:B4:74:B1:55の場合
target_compile_definitions(main PRIVATE "REMOTE_BD_ADDR=0x98, 0xB6, 0xB4, 0x74, 0xB1, 0x55")
