cmake_minimum_required(VERSION 3.20)

project(virtual_badge_avr C)

set(MCU atmega328p)
set(F_CPU 16000000UL)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/cmake/avr-gcc-toolchain.cmake)

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/display.c
    src/card_reader.c
    src/hal/hal_atmega328p.c
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
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/flash.sh ${PROJECT_NAME}.hex
    DEPENDS ${PROJECT_NAME}.elf
    USES_TERMINAL
)
