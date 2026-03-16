#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#include <util/delay.h>

#include "card_reader.h"
#include "display.h"

// FAT32 root filenames (8.3, uppercase)
#define SD_IMAGE1_NAME "HDEOMC.RAW"
#define SD_IMAGE2_NAME "HUEOMC.RAW"

#ifndef TFT_WIDTH
#define TFT_WIDTH 128u
#endif
#ifndef TFT_HEIGHT
#define TFT_HEIGHT 160u
#endif

int main(void) {
    display_init();
    display_fill_color(0x0000);

    card_reader_status_t status = {0};
    card_reader_init(&status);
    card_reader_print_status(&status);

    while (1) {
        if (status.sd_ok && status.fat_ok) {
            card_reader_draw_raw565(SD_IMAGE1_NAME, TFT_WIDTH, TFT_HEIGHT);
            _delay_ms(500);
            card_reader_draw_raw565(SD_IMAGE2_NAME, TFT_WIDTH, TFT_HEIGHT);
            _delay_ms(500);
        } else {
            _delay_ms(200);
        }

        card_reader_handle_cli(&status, SD_IMAGE1_NAME, SD_IMAGE2_NAME, TFT_WIDTH, TFT_HEIGHT);
    }
}
