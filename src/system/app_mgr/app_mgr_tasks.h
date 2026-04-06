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
    app_mgr_task_role_t role;
    uint8_t auto_start;
} app_mgr_task_desc_t;

const app_mgr_task_desc_t *app_mgr_tasks_find(app_mgr_app_id_t app_id);
const app_mgr_task_desc_t *app_mgr_tasks_get(uint8_t index);
uint8_t app_mgr_tasks_count(void);
#else
typedef struct {
    app_mgr_app_id_t app_id;
} app_mgr_task_desc_t;

const app_mgr_task_desc_t *app_mgr_tasks_find(app_mgr_app_id_t app_id);
const app_mgr_task_desc_t *app_mgr_tasks_get(uint8_t index);
uint8_t app_mgr_tasks_count(void);
#endif
