#include "system/app_mgr/app_mgr_tasks.h"

#include "apps/animator/animator.h"

#if defined(ESP_PLATFORM)
enum {
    APP_MGR_TASK_STACK_WORDS = 4096,
    APP_MGR_TASK_PRIORITY = 5,
};

static const app_mgr_task_desc_t k_app_mgr_tasks[] = {
    {
        .app_id = APP_MGR_APP_MAIN,
        .task_name = "main_app",
        .entry_fn = animator_app_task,
        .stack_words = APP_MGR_TASK_STACK_WORDS,
        .priority = APP_MGR_TASK_PRIORITY,
        .task_ctx = 0,
    },
};

/************************************************
* app_mgr_tasks_find
* Returns the task descriptor for a registered application id.
* Parameters: app_id = application to look up.
* Returns: descriptor pointer, or NULL when not found.
***************************************************/
const app_mgr_task_desc_t *app_mgr_tasks_find(app_mgr_app_id_t app_id)
{
    for (uint8_t i = 0; i < (sizeof(k_app_mgr_tasks) / sizeof(k_app_mgr_tasks[0])); i++)
    {
        if (k_app_mgr_tasks[i].app_id == app_id)
        {
            return &k_app_mgr_tasks[i];
        }
    }
    return 0;
}
#else
const app_mgr_task_desc_t *app_mgr_tasks_find(app_mgr_app_id_t app_id)
{
    (void)app_id;
    return 0;
}
#endif
