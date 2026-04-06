set(DEBUG_APP_ENABLED 0)
if(DEFINED DEBUG_APP)
    set(DEBUG_APP_ENABLED ${DEBUG_APP})
endif()

set(APP_SRCS
    "main.c"
    "drivers/display/display_api.c"
    "drivers/display/display_core.c"
    "drivers/display/display_spi.c"
    "drivers/card_reader/card_reader_api.c"
    "drivers/card_reader/card_reader_core.c"
    "drivers/card_reader/card_reader_fat.c"
    "drivers/card_reader/card_reader_spi.c"
    "apps/animator/animator.c"
    "system/render/render_api.c"
    "system/render/render_core.c"
    "system/render/render_image.c"
    "system/render/render_text.c"
    "system/app_mgr/app_mgr_api.c"
    "system/app_mgr/app_mgr_core.c"
    "system/app_mgr/app_mgr_tasks.c"
    "hal/hal_esp32_idf.c"
)

if(DEBUG_APP_ENABLED)
    list(APPEND APP_SRCS "apps/debug/debug.c")
endif()

idf_component_register(
    SRCS
        ${APP_SRCS}
    INCLUDE_DIRS
        "."
        "hal"
        "drivers"
        "drivers/card_reader"
        "apps"
    REQUIRES
        driver
)

if(DEBUG_APP_ENABLED)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE DEBUG_APP_ENABLED)
endif()
