#include "system/app_mgr/app_mgr_api.h"
#include "system/app_mgr/app_mgr_core.h"

/************************************************
* app_mgr_init
* Initializes the application manager state.
* Parameters: state = manager state, startup_app = app to launch on startup.
* Returns: void.
***************************************************/
void app_mgr_init(app_mgr_state_t *state, app_mgr_app_id_t startup_app)
{
    app_mgr_core_init(state, startup_app);
}

/************************************************
* app_mgr_launch
* Launches the app described by the provided state and returns its handle.
* Parameters: state = app state describing the app to launch.
* Returns: launched app handle on success, NULL on failure.
***************************************************/
app_mgr_state_t *app_mgr_launch(app_mgr_state_t *state)
{
    return app_mgr_core_launch(state);
}

/************************************************
* app_mgr_stop
* Stops the task referenced by the provided app handle.
* Parameters: state = app handle returned by app_mgr_launch.
* Returns: 1 on success, 0 on failure.
***************************************************/
uint8_t app_mgr_stop(app_mgr_state_t *state)
{
    return app_mgr_core_stop(state);
}

/************************************************
* app_mgr_get_active
* Reports the currently active app identity.
* Parameters: state = manager state.
* Returns: active app id (APP_MGR_APP_NONE when state is NULL).
***************************************************/
app_mgr_app_id_t app_mgr_get_active(const app_mgr_state_t *state)
{
    return app_mgr_core_get_active(state);
}
