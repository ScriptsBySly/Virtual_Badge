#include "system/app_mgr/app_mgr_core.h"
#include "system/app_mgr/app_mgr_tasks.h"

#include <stdlib.h>

#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

/************************************************
* app_mgr_core_init
* Resets the manager state and records the configured startup app.
* Parameters: state = manager state, startup_app = app to launch at startup.
* Returns: void.
***************************************************/
void app_mgr_core_init(app_mgr_state_t *state, app_mgr_app_id_t startup_app)
{
    uint8_t i = 0;

    if (!state)
    {
        return;
    }

    state->startup_app = startup_app;
    state->active_app = APP_MGR_APP_NONE;
    state->app_id = 0;
    state->active_task_handle = 0;
    for (i = 0; i < APP_MGR_MAX_SERVICE_TASKS; i++)
    {
        state->services[i].app_id = APP_MGR_APP_NONE;
        state->services[i].task_handle = 0;
    }
}

/************************************************
* app_mgr_core_startup
* Launches the configured startup app or keeps the main task active.
* Parameters: state = manager state.
* Returns: 1 on success, 0 on failure.
***************************************************/
uint8_t app_mgr_core_startup(app_mgr_state_t *state)
{
    if (!state)
    {
        return 0;
    }
    if (state->startup_app == APP_MGR_APP_NONE)
    {
        state->startup_app = APP_MGR_APP_MAIN;
    }
    return app_mgr_core_launch(state) != 0;
}

#if defined(ESP_PLATFORM)
typedef struct {
    app_mgr_state_t *app_state;
    app_mgr_app_id_t app_id;
    app_mgr_task_entry_t entry_fn;
    void *task_ctx;
    app_mgr_task_role_t role;
} app_mgr_task_runtime_t;

static void app_mgr_core_task_trampoline(void *arg);
static app_mgr_state_t *app_mgr_core_launch_registered_app(app_mgr_state_t *state, app_mgr_app_id_t app_id);
static int8_t app_mgr_core_find_service_slot(const app_mgr_state_t *state, app_mgr_app_id_t app_id);
static app_mgr_service_slot_t *app_mgr_core_alloc_service_slot(app_mgr_state_t *state, app_mgr_app_id_t app_id);
static uint8_t app_mgr_core_launch_service(app_mgr_state_t *state, const app_mgr_task_desc_t *desc);
static uint8_t app_mgr_core_launch_auto_services(app_mgr_state_t *state);
static void app_mgr_core_handle_task_failure(app_mgr_state_t *state, app_mgr_app_id_t failed_app_id);
static app_mgr_state_t *g_app_mgr_main_state = 0;
static app_mgr_state_t *g_app_mgr_active_state = 0;
static uint32_t g_app_mgr_next_id = 1u;

/************************************************
* app_mgr_core_stop_task_app
* Deletes the currently active task-backed app if one is running.
* Parameters: state = manager state.
* Returns: 1 on success, 0 on failure.
***************************************************/
static uint8_t app_mgr_core_stop_task_app(app_mgr_state_t *state)
{
    if (!state)
    {
        return 0;
    }
    if (!state->active_task_handle)
    {
        return 1;
    }

    if (state->active_task_handle == xTaskGetCurrentTaskHandle())
    {
        state->active_task_handle = 0;
        state->active_app = APP_MGR_APP_NONE;
        state->app_id = 0;
        if (g_app_mgr_active_state == state)
        {
            g_app_mgr_active_state = 0;
        }
        vTaskDelete(0);
        return 1;
    }

    vTaskDelete(state->active_task_handle);
    state->active_task_handle = 0;
    state->active_app = APP_MGR_APP_NONE;
    state->app_id = 0;
    if (g_app_mgr_active_state == state)
    {
        g_app_mgr_active_state = 0;
    }
    return 1;
}

static int8_t app_mgr_core_find_service_slot(const app_mgr_state_t *state, app_mgr_app_id_t app_id)
{
    if (!state)
    {
        return -1;
    }

    for (uint8_t i = 0; i < APP_MGR_MAX_SERVICE_TASKS; i++)
    {
        if (state->services[i].app_id == app_id)
        {
            return (int8_t)i;
        }
    }

    return -1;
}

static app_mgr_service_slot_t *app_mgr_core_alloc_service_slot(app_mgr_state_t *state, app_mgr_app_id_t app_id)
{
    int8_t slot_index = app_mgr_core_find_service_slot(state, app_id);

    if (!state)
    {
        return 0;
    }

    if (slot_index >= 0)
    {
        return &state->services[(uint8_t)slot_index];
    }

    for (uint8_t i = 0; i < APP_MGR_MAX_SERVICE_TASKS; i++)
    {
        if (state->services[i].app_id == APP_MGR_APP_NONE)
        {
            state->services[i].app_id = app_id;
            state->services[i].task_handle = 0;
            return &state->services[i];
        }
    }

    return 0;
}

static uint8_t app_mgr_core_launch_service(app_mgr_state_t *state, const app_mgr_task_desc_t *desc)
{
    app_mgr_task_runtime_t *runtime = 0;
    app_mgr_service_slot_t *slot = 0;

    if (!state || !desc || !desc->entry_fn || desc->role != APP_MGR_TASK_ROLE_SERVICE)
    {
        return 0;
    }

    slot = app_mgr_core_alloc_service_slot(state, desc->app_id);
    if (!slot)
    {
        return 0;
    }
    if (slot->task_handle)
    {
        return 1;
    }

    runtime = (app_mgr_task_runtime_t *)malloc(sizeof(*runtime));
    if (!runtime)
    {
        return 0;
    }

    runtime->app_state = state;
    runtime->app_id = desc->app_id;
    runtime->entry_fn = desc->entry_fn;
    runtime->task_ctx = desc->task_ctx;
    runtime->role = desc->role;

    if (xTaskCreate(app_mgr_core_task_trampoline,
                    desc->task_name,
                    desc->stack_words,
                    runtime,
                    desc->priority,
                    &slot->task_handle) != pdPASS)
    {
        free(runtime);
        slot->task_handle = 0;
        return 0;
    }

    return 1;
}

static uint8_t app_mgr_core_launch_auto_services(app_mgr_state_t *state)
{
    for (uint8_t i = 0; i < app_mgr_tasks_count(); i++)
    {
        const app_mgr_task_desc_t *desc = app_mgr_tasks_get(i);
        if (!desc || desc->role != APP_MGR_TASK_ROLE_SERVICE || !desc->auto_start)
        {
            continue;
        }
        if (!app_mgr_core_launch_service(state, desc))
        {
            return 0;
        }
    }

    return 1;
}

static app_mgr_state_t *app_mgr_core_launch_registered_app(app_mgr_state_t *state, app_mgr_app_id_t app_id)
{
    const app_mgr_task_desc_t *desc = app_mgr_tasks_find(app_id);
    app_mgr_task_runtime_t *runtime = 0;

    if (!state || !desc || !desc->entry_fn)
    {
        return 0;
    }

    runtime = (app_mgr_task_runtime_t *)malloc(sizeof(*runtime));
    if (!runtime)
    {
        return 0;
    }

    runtime->app_state = state;
    runtime->app_id = app_id;
    runtime->entry_fn = desc->entry_fn;
    runtime->task_ctx = desc->task_ctx;
    runtime->role = desc->role;

    if (xTaskCreate(app_mgr_core_task_trampoline,
                    desc->task_name,
                    desc->stack_words,
                    runtime,
                    desc->priority,
                    &state->active_task_handle) != pdPASS)
    {
        free(runtime);
        return 0;
    }

    state->active_app = app_id;
    state->app_id = g_app_mgr_next_id++;
    g_app_mgr_active_state = state;
    return state;
}

static void app_mgr_core_handle_task_failure(app_mgr_state_t *state, app_mgr_app_id_t failed_app_id)
{
    if (!state)
    {
        return;
    }

    state->active_task_handle = 0;
    state->active_app = APP_MGR_APP_NONE;
    state->app_id = 0;
    if (g_app_mgr_active_state == state)
    {
        g_app_mgr_active_state = 0;
    }

    if (failed_app_id == APP_MGR_APP_MAIN)
    {
        if (g_app_mgr_main_state)
        {
            (void)app_mgr_core_launch_registered_app(g_app_mgr_main_state, APP_MGR_APP_MAIN);
        }
        return;
    }

    if (g_app_mgr_main_state)
    {
        (void)app_mgr_core_launch_registered_app(g_app_mgr_main_state, APP_MGR_APP_MAIN);
    }
}

/************************************************
* app_mgr_core_task_trampoline
* Runs a registered app entry and routes failures through manager policy.
* Parameters: arg = runtime wrapper containing manager state and app entry.
* Returns: never returns.
***************************************************/
static void app_mgr_core_task_trampoline(void *arg)
{
    app_mgr_task_runtime_t *runtime = (app_mgr_task_runtime_t *)arg;
    uint8_t ok = 0;
    app_mgr_state_t *state = 0;
    app_mgr_app_id_t app_id = APP_MGR_APP_NONE;
    app_mgr_task_role_t role = APP_MGR_TASK_ROLE_APP;

    if (!runtime || !runtime->entry_fn)
    {
        vTaskDelete(0);
        return;
    }

    state = runtime->app_state;
    app_id = runtime->app_id;
    role = runtime->role;
    ok = runtime->entry_fn(runtime->task_ctx);
    free(runtime);

    if (!ok)
    {
        if (role == APP_MGR_TASK_ROLE_SERVICE)
        {
            if (state)
            {
                int8_t slot_index = app_mgr_core_find_service_slot(state, app_id);
                const app_mgr_task_desc_t *desc = app_mgr_tasks_find(app_id);

                if (slot_index >= 0)
                {
                    state->services[(uint8_t)slot_index].task_handle = 0;
                }
                if (desc)
                {
                    (void)app_mgr_core_launch_service(state, desc);
                }
            }
        }
        else
        {
            app_mgr_core_handle_task_failure(state, app_id);
        }
    }
    else if (state)
    {
        if (role == APP_MGR_TASK_ROLE_SERVICE)
        {
            int8_t slot_index = app_mgr_core_find_service_slot(state, app_id);
            if (slot_index >= 0)
            {
                state->services[(uint8_t)slot_index].task_handle = 0;
            }
        }
        else
        {
            state->active_task_handle = 0;
            state->active_app = APP_MGR_APP_NONE;
            state->app_id = 0;
            if (g_app_mgr_active_state == state)
            {
                g_app_mgr_active_state = 0;
            }
        }
    }

    vTaskDelete(0);
}
#endif

/************************************************
* app_mgr_core_launch
* Launches the app described by the provided state and returns its handle.
* Parameters: state = app state describing the app to launch.
* Returns: launched app handle on success, NULL on failure.
***************************************************/
app_mgr_state_t *app_mgr_core_launch(app_mgr_state_t *state)
{
    app_mgr_app_id_t app_id = APP_MGR_APP_NONE;

    if (!state)
    {
        return 0;
    }

    if (!g_app_mgr_main_state || state->startup_app == APP_MGR_APP_MAIN)
    {
        g_app_mgr_main_state = state;
    }

    app_id = state->startup_app;
    if (app_id == APP_MGR_APP_NONE)
    {
        app_id = APP_MGR_APP_MAIN;
    }

#if defined(ESP_PLATFORM)
    if (!app_mgr_core_launch_auto_services(state))
    {
        return 0;
    }

    if (g_app_mgr_active_state && g_app_mgr_active_state != state)
    {
        if (!app_mgr_core_stop_task_app(g_app_mgr_active_state))
        {
            return 0;
        }
    }
    else if (g_app_mgr_active_state == state && state->active_task_handle)
    {
        if (!app_mgr_core_stop_task_app(state))
        {
            return 0;
        }
    }

    return app_mgr_core_launch_registered_app(state, app_id);
#else
    state->active_app = app_id;
    state->app_id = 1u;
    return state;
#endif
}

/************************************************
* app_mgr_core_stop
* Stops the task referenced by the provided app handle.
* Parameters: state = app handle returned by launch.
* Returns: 1 on success, 0 on failure.
***************************************************/
uint8_t app_mgr_core_stop(app_mgr_state_t *state)
{
    if (!state)
    {
        return 0;
    }

#if defined(ESP_PLATFORM)
    if (g_app_mgr_active_state != state || state->app_id == 0 || !state->active_task_handle)
    {
        return 0;
    }

    if (state->active_task_handle == xTaskGetCurrentTaskHandle())
    {
        app_mgr_core_handle_task_failure(state, state->active_app);
        vTaskDelete(0);
        return 1;
    }

    if (!app_mgr_core_stop_task_app(state))
    {
        return 0;
    }
    app_mgr_core_handle_task_failure(state, state->active_app);
    return 1;
#else
    if (state->app_id == 0)
    {
        return 0;
    }
    state->app_id = 0;
    state->active_app = APP_MGR_APP_MAIN;
    return 1;
#endif
}

/************************************************
* app_mgr_core_get_active
* Returns the active application id tracked by the manager.
* Parameters: state = manager state.
* Returns: active app id, or APP_MGR_APP_NONE when state is NULL.
***************************************************/
app_mgr_app_id_t app_mgr_core_get_active(const app_mgr_state_t *state)
{
    if (!state)
    {
        return APP_MGR_APP_NONE;
    }
    return state->active_app;
}
