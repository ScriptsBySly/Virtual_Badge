#pragma once

#include <stdint.h>

#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

enum {
    APP_MGR_MAX_SERVICE_TASKS = 4u,
};

typedef enum {
    APP_MGR_APP_NONE = 0,
    APP_MGR_APP_RENDER,
    APP_MGR_APP_MAIN,
    APP_MGR_APP_DEBUG,
} app_mgr_app_id_t;

typedef enum {
    APP_MGR_TASK_ROLE_APP = 0,
    APP_MGR_TASK_ROLE_SERVICE,
} app_mgr_task_role_t;

typedef struct {
    app_mgr_app_id_t app_id;
#if defined(ESP_PLATFORM)
    TaskHandle_t task_handle;
#else
    void *task_handle;
#endif
} app_mgr_service_slot_t;

typedef struct {
    app_mgr_app_id_t startup_app;
    app_mgr_app_id_t active_app;
    uint32_t app_id;
#if defined(ESP_PLATFORM)
    TaskHandle_t active_task_handle;
#else
    void *active_task_handle;
#endif
    app_mgr_service_slot_t services[APP_MGR_MAX_SERVICE_TASKS];
} app_mgr_state_t;
