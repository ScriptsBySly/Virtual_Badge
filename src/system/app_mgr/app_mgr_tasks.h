#pragma once

#include <stdint.h>

#include "system/app_mgr/app_mgr_common.h"

#if defined(ESP_PLATFORM)
typedef uint8_t (*app_mgr_task_entry_t)(void *ctx);

typedef struct {
    app_mgr_app_id_t app_id;
    const char *task_name;
    app_mgr_task_entry_t entry_fn;
    uint16_t stack_words;
    UBaseType_t priority;
    void *task_ctx;
} app_mgr_task_desc_t;

const app_mgr_task_desc_t *app_mgr_tasks_find(app_mgr_app_id_t app_id);
#else
typedef struct {
    app_mgr_app_id_t app_id;
} app_mgr_task_desc_t;

const app_mgr_task_desc_t *app_mgr_tasks_find(app_mgr_app_id_t app_id);
#endif
