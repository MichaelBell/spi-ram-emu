cmake_minimum_required(VERSION 3.12)

# Change your executable name to something creative!
set(NAME spi-ram-emu) # <-- Name your project/executable here!

#include(pimoroni_pico_import.cmake)
include(pico_sdk_import.cmake)

# Gooey boilerplate
project(${NAME} C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

#set(PICO_COPY_TO_RAM 1)

# Initialize the SDK
pico_sdk_init()

add_compile_options(-Wall
        -Wno-format
        -Wno-unused-function
        -Wno-maybe-uninitialized
        )

# Add your source files
add_executable(${NAME}
    main.c # <-- Add source files here!
    sram.c
    msc_disk.c
    usb_descriptors.c
)

# Include required libraries
# This assumes `pimoroni-pico` is stored alongside your project
#include(common/pimoroni_i2c)
#include(common/pimoroni_bus)
#include(libraries/bitmap_fonts/bitmap_fonts)
#include(libraries/hershey_fonts/hershey_fonts)
#include(libraries/pico_explorer/pico_explorer)

# Don't forget to link the libraries you need!
target_link_libraries(${NAME}
    pico_stdlib
    pico_multicore
    hardware_dma
    hardware_pio
    hardware_watchdog
    tinyusb_device tinyusb_board
)

# Make sure TinyUSB can find tusb_config.h
target_include_directories(${NAME} PUBLIC
        ${CMAKE_CURRENT_LIST_DIR})

set_target_properties(${NAME} PROPERTIES PICO_TARGET_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/sram_memmap.ld)
pico_add_link_depend(${NAME} ${CMAKE_CURRENT_LIST_DIR}/sram_memmap.ld)

pico_generate_pio_header(${NAME} ${CMAKE_CURRENT_LIST_DIR}/sram.pio)

# create map/bin/hex file etc.
pico_add_extra_outputs(${NAME})

pico_enable_stdio_usb(${NAME} 1)
pico_enable_stdio_uart(${NAME} 0)

# Set up files for the release packages
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.uf2
    ${CMAKE_CURRENT_LIST_DIR}/README.md
    DESTINATION .
)

set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_GENERATOR "ZIP" "TGZ")
include(CPack)
