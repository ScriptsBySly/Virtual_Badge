#pragma once

#include <stdint.h>

#include "system/app_mgr/app_mgr_common.h"

void app_mgr_init(app_mgr_state_t *state, app_mgr_app_id_t startup_app);
app_mgr_state_t *app_mgr_launch(app_mgr_state_t *state);
uint8_t app_mgr_stop(app_mgr_state_t *state);
app_mgr_app_id_t app_mgr_get_active(const app_mgr_state_t *state);
