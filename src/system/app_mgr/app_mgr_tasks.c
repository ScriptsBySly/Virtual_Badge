#include "system/app_mgr/app_mgr_tasks.h"

#include "apps/animator/animator.h"
#include "system/render/render_api.h"
#ifdef DEBUG_APP_ENABLED
#include "apps/debug/debug.h"
#endif

#if defined(ESP_PLATFORM)
enum {
    APP_MGR_TASK_STACK_WORDS = 4096,
    APP_MGR_TASK_PRIORITY = 5,
};

static const app_mgr_task_desc_t k_app_mgr_tasks[] = {
    {
        .app_id = APP_MGR_APP_RENDER,
        .task_name = "render",
        .entry_fn = render_app_task,
        .stack_words = APP_MGR_TASK_STACK_WORDS,
        .priority = APP_MGR_TASK_PRIORITY,
        .task_ctx = 0,
        .role = APP_MGR_TASK_ROLE_SERVICE,
        .auto_start = 1,
        .uses_display = 0,
    },
#ifdef DEBUG_APP_ENABLED
    {
        .app_id = APP_MGR_APP_DEBUG,
        .task_name = "debug",
        .entry_fn = debug_app_task,
        .stack_words = APP_MGR_TASK_STACK_WORDS,
        .priority = APP_MGR_TASK_PRIORITY,
        .task_ctx = 0,
        .role = APP_MGR_TASK_ROLE_APP,
        .auto_start = 0,
        .uses_display = 1,
    },
#else
    {
        .app_id = APP_MGR_APP_MAIN,
        .task_name = "main_app",
        .entry_fn = animator_app_task,
        .stack_words = APP_MGR_TASK_STACK_WORDS,
        .priority = APP_MGR_TASK_PRIORITY,
        .task_ctx = 0,
        .role = APP_MGR_TASK_ROLE_APP,
        .auto_start = 0,
        .uses_display = 1,
    },
#endif
};

/************************************************
* app_mgr_tasks_find
* Returns the task descriptor for a registered application id.
* Parameters: app_id = application to look up.
* Returns: descriptor pointer, or NULL when not found.
***************************************************/
const app_mgr_task_desc_t *app_mgr_tasks_find(app_mgr_app_id_t app_id)
{
    for (uint8_t i = 0; i < app_mgr_tasks_count(); i++)
    {
        if (k_app_mgr_tasks[i].app_id == app_id)
        {
            return &k_app_mgr_tasks[i];
        }
    }
    return 0;
}

const app_mgr_task_desc_t *app_mgr_tasks_get(uint8_t index)
{
    if (index >= app_mgr_tasks_count())
    {
        return 0;
    }
    return &k_app_mgr_tasks[index];
}

uint8_t app_mgr_tasks_count(void)
{
    return (uint8_t)(sizeof(k_app_mgr_tasks) / sizeof(k_app_mgr_tasks[0]));
}
#else
const app_mgr_task_desc_t *app_mgr_tasks_find(app_mgr_app_id_t app_id)
{
    (void)app_id;
    return 0;
}

const app_mgr_task_desc_t *app_mgr_tasks_get(uint8_t index)
{
    (void)index;
    return 0;
}

uint8_t app_mgr_tasks_count(void)
{
    return 0;
}
#endif
