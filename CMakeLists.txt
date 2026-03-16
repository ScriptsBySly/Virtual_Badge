cmake_minimum_required(VERSION 3.20)

project(virtual_badge_avr C)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(MCU atmega328p)
set(F_CPU 16000000UL)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/cmake/avr-gcc-toolchain.cmake)

set(GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
set(GENERATED_IMAGES_H ${GENERATED_DIR}/generated_images.h)

add_custom_command(
    OUTPUT ${GENERATED_IMAGES_H}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/png_to_rgb565.py
            --out ${GENERATED_IMAGES_H}
            --resize 128x160
            --rle
            ${CMAKE_SOURCE_DIR}/HD_EO_MC.png
            ${CMAKE_SOURCE_DIR}/HU_EO_MC.png
    DEPENDS
        ${CMAKE_SOURCE_DIR}/tools/png_to_rgb565.py
        ${CMAKE_SOURCE_DIR}/HD_EO_MC.png
        ${CMAKE_SOURCE_DIR}/HU_EO_MC.png
    COMMENT "Generating image header (RLE)"
    VERBATIM
)

add_custom_target(generate_images DEPENDS ${GENERATED_IMAGES_H})

add_executable(${PROJECT_NAME}.elf
    src/main.c
    ${GENERATED_IMAGES_H}
)

target_compile_definitions(${PROJECT_NAME}.elf PRIVATE F_CPU=${F_CPU})

add_dependencies(${PROJECT_NAME}.elf generate_images)

target_include_directories(${PROJECT_NAME}.elf PRIVATE ${GENERATED_DIR})

target_compile_options(${PROJECT_NAME}.elf PRIVATE
    -mmcu=${MCU}
    -Os
    -Wall
    -Wextra
    -Werror
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
