#pragma once

#include <stdint.h>

#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

typedef enum {
    APP_MGR_APP_NONE = 0,
    APP_MGR_APP_RENDER,
    APP_MGR_APP_MAIN,
    APP_MGR_APP_DEBUG,
} app_mgr_app_id_t;

typedef struct {
    app_mgr_app_id_t startup_app;
    app_mgr_app_id_t active_app;
    uint32_t app_id;
#if defined(ESP_PLATFORM)
    TaskHandle_t active_task_handle;
    TaskHandle_t service_task_handle;
#else
    void *active_task_handle;
    void *service_task_handle;
#endif
} app_mgr_state_t;
