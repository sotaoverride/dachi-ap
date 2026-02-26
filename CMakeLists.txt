cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)
project(pico_w_provisioning C CXX ASM)
pico_sdk_init()

add_executable(wifi_app main.c)
target_include_directories(wifi_app PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/../pico/pico-examples/pico_w/wifi)
target_link_libraries(wifi_app 
    pico_stdlib 
    pico_cyw43_arch_lwip_threadsafe_background
    hardware_flash
)

pico_enable_stdio_usb(wifi_app 1)
pico_enable_stdio_uart(wifi_app 0) 
pico_add_extra_outputs(wifi_app)

