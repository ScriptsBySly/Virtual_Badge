#include "system/app_mgr/app_mgr_api.h"

#include "hal/hal.h"

#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

static app_mgr_state_t g_main_app_state;

static void app_bootstrap(void)
{
    app_mgr_init(&g_main_app_state, APP_MGR_APP_MAIN);
    (void)app_mgr_launch(&g_main_app_state);
}

#ifdef ESP_PLATFORM
void app_main(void)
{
    app_bootstrap();
    for (;;)
    {
        hal_delay_ms(1000);
    }
}
#else
int main(void)
{
    app_bootstrap();
    return 0;
}
#endif
