cmake_minimum_required(VERSION 3.13)

# 导入 SDK 逻辑 (必须在 project 之前)
include(pico_sdk_import.cmake)

# 定义项目名
project(pico_ds5_ctrl C CXX ASM)

# 初始化 SDK (这步完成后，pico_add_extra_outputs 才会生效)
pico_sdk_init()

add_executable(pico_ds5_ctrl
    main.cpp
    bt.cpp
    tusb_ctrl.cpp
)

# 包含配置文件路径
target_include_directories(pico_ds5_ctrl PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

target_compile_definitions(pico_ds5_ctrl PRIVATE
        CYW43_LWIP=0
        PICO_FLASH_ASSUME_CORE1_SAFE=1
)

pico_enable_stdio_uart(pico_ds5_ctrl 0)
pico_enable_stdio_usb(pico_ds5_ctrl 0)

target_link_libraries(pico_ds5_ctrl PUBLIC 
    pico_stdlib 
    pico_unique_id 
    tinyusb_device 
    tinyusb_board
    pico_multicore
    pico_btstack_classic
    pico_cyw43_arch_poll
    pico_btstack_cyw43
    hardware_interp
)

pico_add_extra_outputs(pico_ds5_ctrl)