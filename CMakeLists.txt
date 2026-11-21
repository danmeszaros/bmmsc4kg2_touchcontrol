cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PICO_BOARD waveshare_rp2040_touch_lcd_1.28 CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(touchcontrol_fw C CXX ASM)

pico_sdk_init()

set(SRC ${CMAKE_CURRENT_LIST_DIR}/src)

add_executable(touchcontrol_fw
    ${SRC}/main.cc
    ${SRC}/camera/camera.cc
    ${SRC}/camera/json.cc
    ${SRC}/gfx/gfx.cc
    ${SRC}/gfx/fonts_gen.cc
    ${SRC}/ui/ui.cc
    ${SRC}/ui/format.cc
    ${SRC}/ui/gesture.cc
    ${SRC}/hw/touch.cc
    ${SRC}/hw/lcd.cc
    ${SRC}/hw/buttons.cc
    ${SRC}/hw/log.c
    ${SRC}/hw/usb_diag.c
    ${SRC}/net/usb_network.c
    ${SRC}/net/usb_descriptors.c
    ${SRC}/net/http_client.cc
    ${SRC}/net/http_parser.cc
    ${SRC}/net/dhcpserver/dhcpserver.c
    ${PICO_TINYUSB_PATH}/lib/networking/rndis_reports.c
)

pico_generate_pio_header(touchcontrol_fw ${SRC}/hw/uart_tx.pio)

pico_set_program_name(touchcontrol_fw "touchcontrol_fw")
pico_set_program_version(touchcontrol_fw "0.1")

# USB belongs to the camera link; printf goes to the PIO UART log (src/hw/log.c)
pico_enable_stdio_uart(touchcontrol_fw 0)
pico_enable_stdio_usb(touchcontrol_fw 0)

target_compile_options(touchcontrol_fw PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers>)

target_include_directories(touchcontrol_fw PRIVATE
    ${PICO_TINYUSB_PATH}/lib/networking # rndis_protocol.h
    ${SRC} ${SRC}/net ${SRC}/camera ${SRC}/gfx ${SRC}/ui ${SRC}/hw # net: lwipopts.h, tusb_config.h
)

target_link_libraries(touchcontrol_fw
    pico_stdlib
    hardware_spi
    hardware_i2c
    hardware_dma
    hardware_pio
    hardware_pwm
    hardware_watchdog
    tinyusb_device
    pico_lwip
    pico_lwip_nosys
    pico_unique_id
)

pico_add_extra_outputs(touchcontrol_fw)
