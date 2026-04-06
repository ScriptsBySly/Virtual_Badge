#include "system/app_mgr/app_mgr_tasks.h"

#include "hal/hal.h"

#if defined(ESP_PLATFORM)
enum {
    APP_MGR_TASK_STACK_WORDS = 4096,
    APP_MGR_TASK_PRIORITY = 5,
    APP_MGR_DUMMY_DELAY_MS = 1000,
};

/************************************************
* app_mgr_main_dummy_task
* Placeholder main app task used until the real main app is registered.
* Parameters: arg = unused placeholder context.
* Returns: 1 when the task exits cleanly.
***************************************************/
static uint8_t app_mgr_main_dummy_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        hal_delay_ms(APP_MGR_DUMMY_DELAY_MS);
    }
    return 1;
}

/************************************************
* app_mgr_render_dummy_task
* Placeholder render app task used until the real renderer exists.
* Parameters: arg = unused placeholder context.
* Returns: 1 when the task exits cleanly.
***************************************************/
static uint8_t app_mgr_render_dummy_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        hal_delay_ms(APP_MGR_DUMMY_DELAY_MS);
    }
    return 1;
}

/************************************************
* app_mgr_badge_dummy_task
* Placeholder badge app task used until the real badge app exists.
* Parameters: arg = unused placeholder context.
* Returns: 1 when the task exits cleanly.
***************************************************/
static uint8_t app_mgr_badge_dummy_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        hal_delay_ms(APP_MGR_DUMMY_DELAY_MS);
    }
    return 1;
}

static const app_mgr_task_desc_t k_app_mgr_tasks[] = {
    {
        .app_id = APP_MGR_APP_MAIN,
        .task_name = "main_app",
        .entry_fn = app_mgr_main_dummy_task,
        .stack_words = APP_MGR_TASK_STACK_WORDS,
        .priority = APP_MGR_TASK_PRIORITY,
        .task_ctx = 0,
    },
    {
        .app_id = APP_MGR_APP_RENDER_DUMMY,
        .task_name = "render_app",
        .entry_fn = app_mgr_render_dummy_task,
        .stack_words = APP_MGR_TASK_STACK_WORDS,
        .priority = APP_MGR_TASK_PRIORITY,
        .task_ctx = 0,
    },
    {
        .app_id = APP_MGR_APP_BADGE_DUMMY,
        .task_name = "badge_app",
        .entry_fn = app_mgr_badge_dummy_task,
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
