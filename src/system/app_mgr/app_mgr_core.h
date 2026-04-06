#pragma once

#include <stdint.h>

#include "system/app_mgr/app_mgr_common.h"

void app_mgr_core_init(app_mgr_state_t *state, app_mgr_app_id_t startup_app);
uint8_t app_mgr_core_startup(app_mgr_state_t *state);
app_mgr_state_t *app_mgr_core_launch(app_mgr_state_t *state);
uint8_t app_mgr_core_stop(app_mgr_state_t *state);
app_mgr_app_id_t app_mgr_core_get_active(const app_mgr_state_t *state);
