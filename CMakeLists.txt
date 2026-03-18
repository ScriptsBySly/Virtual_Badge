cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED HAL_MCU)
    set(HAL_MCU "atmega328p" CACHE STRING "MCU HAL selection (atmega328p or esp32)")
endif()

if(HAL_MCU STREQUAL "esp32")
    if(NOT DEFINED ENV{IDF_PATH})
        message(FATAL_ERROR "IDF_PATH not set. Run: . /path/to/esp-idf/export.sh")
    endif()
    set(EXTRA_COMPONENT_DIRS ${CMAKE_SOURCE_DIR}/src)
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
    project(virtual_badge_esp32)
    return()
endif()

project(virtual_badge_avr C)

set(MCU atmega328p)
set(F_CPU 16000000UL)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/cmake/avr-gcc-toolchain.cmake)

set(HAL_SRC src/hal/hal_atmega328p.c)

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/drivers/display.c
    src/drivers/card_reader/card_reader_api.c
    src/drivers/card_reader/card_reader_core.c
    src/drivers/card_reader/card_reader_fat.c
    src/drivers/card_reader/card_reader_spi.c
    src/apps/animator.c
    ${HAL_SRC}
)

target_compile_definitions(${PROJECT_NAME}.elf PRIVATE F_CPU=${F_CPU})

target_compile_options(${PROJECT_NAME}.elf PRIVATE
    -mmcu=${MCU}
    -Os
    -Wall
    -Wextra
    -ffunction-sections
    -fdata-sections
)

target_link_options(${PROJECT_NAME}.elf PRIVATE
    -mmcu=${MCU}
    -Wl,--gc-sections
)

add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex -R .eeprom
            ${PROJECT_NAME}.elf ${PROJECT_NAME}.hex
    BYPRODUCTS ${PROJECT_NAME}.hex
    COMMENT "Generating HEX"
)

add_custom_target(flash
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/flash_nano.sh ${PROJECT_NAME}.hex
    DEPENDS ${PROJECT_NAME}.elf
    USES_TERMINAL
)
