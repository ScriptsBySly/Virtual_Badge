#include "card_reader.h"
#include "display.h"
#include "hal/hal.h"

#ifndef TFT_WIDTH
#define TFT_WIDTH 128u
#endif
#ifndef TFT_HEIGHT
#define TFT_HEIGHT 160u
#endif

#define FRAME_INTERVAL_MS 500u
#define BLINK_STEP_MS 120u
#define TICK_MS 20u

#ifndef TEST_RGB_CYCLE
#define TEST_RGB_CYCLE 0
#endif

#ifndef TEST_SCREEN_DEBUG
#define TEST_SCREEN_DEBUG 0
#endif

static uint16_t rng_state = 0xACE1u;

static uint8_t rng8(void) {
    rng_state = (uint16_t)((rng_state >> 1) ^ (-(rng_state & 1u) & 0xB400u));
    return (uint8_t)(rng_state & 0xFFu);
}

static char *append_str(char *p, const char *s) {
    while (*s) {
        *p++ = *s++;
    }
    return p;
}

static char *append_hex32(char *p, uint32_t v) {
    static const char hex[] = "0123456789ABCDEF";
    *p++ = '0';
    *p++ = 'x';
    for (int8_t i = 7; i >= 0; i--) {
        uint8_t nib = (uint8_t)((v >> (uint8_t)(i * 4)) & 0xFu);
        *p++ = hex[nib];
    }
    return p;
}

static char *append_u8_dec(char *p, uint8_t v) {
    if (v >= 100) {
        *p++ = (char)('0' + (v / 100));
        v = (uint8_t)(v % 100);
        *p++ = (char)('0' + (v / 10));
        *p++ = (char)('0' + (v % 10));
        return p;
    }
    if (v >= 10) {
        *p++ = (char)('0' + (v / 10));
        *p++ = (char)('0' + (v % 10));
        return p;
    }
    *p++ = (char)('0' + v);
    return p;
}

static void build_raw_name(char *out, const char *base, const char *eyes, const char *mouth) {
    out[0] = base[0];
    out[1] = base[1];
    out[2] = eyes[0];
    out[3] = eyes[1];
    out[4] = mouth[0];
    out[5] = mouth[1];
    out[6] = '.';
    out[7] = 'R';
    out[8] = 'A';
    out[9] = 'W';
    out[10] = '\0';
}


static void draw_error_screen(const char *name, const card_reader_status_t *status) {
    display_fill_color(0x0000);
    display_draw_text(0, 0, "SD IMG FAIL", 0xFFFF, 0x0000);
    display_draw_text(0, 8, name, 0xFFFF, 0x0000);
    display_draw_text(0, 16, status->sd_ok ? "SD OK" : "SD FAIL", 0xFFFF, 0x0000);
    display_draw_text(0, 24, status->fat_ok ? "FAT OK" : "FAT NO", 0xFFFF, 0x0000);
    display_draw_text(0, 32, card_reader_sd_fat_format_ok() ? "FATFMT OK" : "FATFMT NO", 0xFFFF, 0x0000);
    display_draw_text(0, 40, card_reader_sd_fat_mount_ok() ? "FATMNT OK" : "FATMNT NO", 0xFFFF, 0x0000);
}

static uint8_t draw_frame(const char *base, const char *eyes, const char *mouth,
                          const card_reader_status_t *status) {
    char name[11];
    build_raw_name(name, base, eyes, mouth);
    uint8_t ok = card_reader_draw_raw565(name, TFT_WIDTH, TFT_HEIGHT);
    if (!ok) {
        draw_error_screen(name, status);
    }
    return ok;
}

typedef enum {
    EVENT_NONE = 0,
    EVENT_HAPPY,
    EVENT_SADNEW,
    EVENT_MAD,
} event_type_t;

static void build_event_name(char *out, const char *base, event_type_t ev) {
    out[0] = base[0];
    out[1] = base[1];
    if (ev == EVENT_HAPPY) {
        out[2] = 'H'; out[3] = 'A'; out[4] = 'P'; out[5] = 'P'; out[6] = 'Y';
        out[7] = '.'; out[8] = 'R'; out[9] = 'A'; out[10] = 'W'; out[11] = '\0';
        return;
    }
    if (ev == EVENT_SADNEW) {
        out[2] = 'S'; out[3] = 'A'; out[4] = 'D'; out[5] = 'N'; out[6] = 'E'; out[7] = 'W';
        out[8] = '.'; out[9] = 'R'; out[10] = 'A'; out[11] = 'W'; out[12] = '\0';
        return;
    }
    // EVENT_MAD
    out[2] = 'M'; out[3] = 'A'; out[4] = 'D';
    out[5] = '.'; out[6] = 'R'; out[7] = 'A'; out[8] = 'W'; out[9] = '\0';
}


static uint8_t draw_event_frame(const char *base, event_type_t ev,
                                const card_reader_status_t *status) {
    char name[13];
    build_event_name(name, base, ev);
    uint8_t ok = card_reader_draw_raw565(name, TFT_WIDTH, TFT_HEIGHT);
    if (!ok) {
        draw_error_screen(name, status);
    }
    return ok;
}

typedef struct {
    uint8_t active;
    uint8_t step;
    uint16_t remaining_ms;
    uint8_t mode;
} blink_state_t;

static void blink_start(blink_state_t *b) {
    b->active = 1;
    b->step = 0;
    b->mode = rng8() % 4u;
    if (b->mode == 2) {
        b->remaining_ms = 1000u;
    } else if (b->mode == 3) {
        b->remaining_ms = 2000u + (uint16_t)(rng8() % 7u) * 1000u;
    } else {
        b->remaining_ms = BLINK_STEP_MS;
    }
}

static const char *blink_eyes_for_step(const blink_state_t *b) {
    if (b->mode == 0) {
        return (b->step == 0) ? "EC" : "EO";
    }
    if (b->mode == 1) {
        if (b->step == 0) return "EM";
        if (b->step == 1) return "EC";
        return "EO";
    }
    if (b->mode == 2) {
        return (b->step == 0) ? "EM" : "EO";
    }
    // mode 3
    return (b->step == 0) ? "EM" : "EO";
}

static uint8_t blink_step_count(const blink_state_t *b) {
    if (b->mode == 0) return 2;
    if (b->mode == 1) return 3;
    return 2;
}

static uint8_t blink_tick(blink_state_t *b, uint16_t tick_ms) {
    if (!b->active) return 0;
    if (b->remaining_ms > tick_ms) {
        b->remaining_ms -= tick_ms;
        return 0;
    }
    b->step++;
    if (b->step >= blink_step_count(b)) {
        b->active = 0;
        return 1;
    }
    b->remaining_ms = BLINK_STEP_MS;
    return 0;
}

static void app_run(void) {
    hal_init();
    display_init();
    display_fill_color(0x0000);

#if TEST_SCREEN_DEBUG
    card_reader_status_t status = {0};
    uint16_t refresh_timer = 0;
    uint16_t init_timer = 0;
    while (1) {
        if (init_timer <= TICK_MS) {
            card_reader_init(&status);
            init_timer = 2000u;
        } else {
            init_timer -= TICK_MS;
        }

        if (refresh_timer <= TICK_MS) {
            refresh_timer = 1000u;
            display_fill_color(0x0000);

            uint8_t line = 0;
            char buf[32];

            display_draw_text(0, (uint16_t)(line++ * 8u), "DEBUG", 0xFFFF, 0x0000);

            char *p = buf;
            p = append_str(p, "SD init:");
            p = append_str(p, status.sd_ok ? "OK" : "FAIL");
            *p = '\0';
            display_draw_text(0, (uint16_t)(line++ * 8u), buf, 0xFFFF, 0x0000);

            p = buf;
            p = append_str(p, "SD type:");
            p = append_str(p, card_reader_sd_is_sdhc() ? "SDHC" : "SDSC");
            *p = '\0';
            display_draw_text(0, (uint16_t)(line++ * 8u), buf, 0xFFFF, 0x0000);

            p = buf;
            p = append_str(p, "FAT:");
            p = append_str(p, card_reader_fat_ready() ? "OK" : "NO");
            p = append_str(p, " SPC:");
            p = append_u8_dec(p, card_reader_fat_sectors_per_cluster());
            *p = '\0';
            display_draw_text(0, (uint16_t)(line++ * 8u), buf, 0xFFFF, 0x0000);

            p = buf;
            p = append_str(p, "LBA:");
            p = append_hex32(p, card_reader_fat_lba_start());
            *p = '\0';
            display_draw_text(0, (uint16_t)(line++ * 8u), buf, 0xFFFF, 0x0000);

            p = buf;
            p = append_str(p, "FAT:");
            p = append_hex32(p, card_reader_fat_first_fat());
            *p = '\0';
            display_draw_text(0, (uint16_t)(line++ * 8u), buf, 0xFFFF, 0x0000);

            p = buf;
            p = append_str(p, "DATA:");
            p = append_hex32(p, card_reader_fat_first_data());
            *p = '\0';
            display_draw_text(0, (uint16_t)(line++ * 8u), buf, 0xFFFF, 0x0000);

            p = buf;
            p = append_str(p, "ROOT:");
            p = append_hex32(p, card_reader_fat_root_cluster());
            *p = '\0';
            display_draw_text(0, (uint16_t)(line++ * 8u), buf, 0xFFFF, 0x0000);

            p = buf;
            p = append_str(p, "CMD0:");
            p = append_hex32(p, card_reader_sd_cmd0_r1());
            *p = '\0';
            display_draw_text(0, (uint16_t)(line++ * 8u), buf, 0xFFFF, 0x0000);

            p = buf;
            p = append_str(p, "CMD8:");
            p = append_hex32(p, card_reader_sd_cmd8_r1());
            *p = '\0';
            display_draw_text(0, (uint16_t)(line++ * 8u), buf, 0xFFFF, 0x0000);

            p = buf;
            p = append_str(p, "R7:");
            p = append_hex32(p, (uint32_t)card_reader_sd_cmd8_r7()[0] << 24 |
                                (uint32_t)card_reader_sd_cmd8_r7()[1] << 16 |
                                (uint32_t)card_reader_sd_cmd8_r7()[2] << 8  |
                                (uint32_t)card_reader_sd_cmd8_r7()[3]);
            *p = '\0';
            display_draw_text(0, (uint16_t)(line++ * 8u), buf, 0xFFFF, 0x0000);

            p = buf;
            p = append_str(p, "ACMD41:");
            p = append_hex32(p, card_reader_sd_acmd41_r1());
            *p = '\0';
            display_draw_text(0, (uint16_t)(line++ * 8u), buf, 0xFFFF, 0x0000);

            p = buf;
            p = append_str(p, "CMD58:");
            p = append_hex32(p, card_reader_sd_cmd58_r1());
            *p = '\0';
            display_draw_text(0, (uint16_t)(line++ * 8u), buf, 0xFFFF, 0x0000);

            p = buf;
            p = append_str(p, "OCR:");
            p = append_hex32(p, (uint32_t)card_reader_sd_cmd58_ocr()[0] << 24 |
                                (uint32_t)card_reader_sd_cmd58_ocr()[1] << 16 |
                                (uint32_t)card_reader_sd_cmd58_ocr()[2] << 8  |
                                (uint32_t)card_reader_sd_cmd58_ocr()[3]);
            *p = '\0';
            display_draw_text(0, (uint16_t)(line++ * 8u), buf, 0xFFFF, 0x0000);

            p = buf;
            p = append_str(p, "MISO:");
            p = append_u8_dec(p, hal_miso_state());
            *p = '\0';
            display_draw_text(0, (uint16_t)(line++ * 8u), buf, 0xFFFF, 0x0000);
        } else {
            refresh_timer -= TICK_MS;
        }

        hal_delay_ms(TICK_MS);
    }
#endif

#if TEST_RGB_CYCLE
    const uint16_t colors[] = {0xF800u, 0x07E0u, 0x001Fu, 0xFFFFu, 0x0000u};
    uint8_t color_index = 0;
    while (1) {
        display_fill_color(colors[color_index]);
        color_index = (uint8_t)((color_index + 1u) % (sizeof(colors) / sizeof(colors[0])));
        hal_delay_ms(500);
    }
#endif

#if !TEST_SCREEN_DEBUG
    card_reader_status_t status = {0};
    card_reader_init(&status);
    card_reader_print_status(&status);

    const char *bases[2] = {"HU", "HD"};
    uint8_t base_index = 0;
    uint16_t frame_timer = FRAME_INTERVAL_MS;
    uint16_t blink_timer = 4u * FRAME_INTERVAL_MS;
    blink_state_t blink = {0};
    const char *base = bases[base_index];
    const char *eyes = "EO";
    draw_frame(base, eyes, "MC", &status);
    event_type_t event = EVENT_NONE;
    uint16_t event_timer = (uint16_t)(5000u + (uint16_t)(rng8() % 6u) * 1000u);
    uint16_t event_remaining = 0;

    while (1) {
        if (status.sd_ok && status.fat_ok) {
            if (frame_timer <= TICK_MS) {
                frame_timer = FRAME_INTERVAL_MS;
                base_index ^= 1u;
                base = bases[base_index];
                if (event != EVENT_NONE) {
                    draw_event_frame(base, event, &status);
                } else {
                    eyes = blink.active ? blink_eyes_for_step(&blink) : "EO";
                    draw_frame(base, eyes, "MC", &status);
                }
            } else {
                frame_timer -= TICK_MS;
            }

            if (event == EVENT_NONE) {
                if (event_timer <= TICK_MS) {
                    event = (event_type_t)(1u + (rng8() % 3u));
                    event_remaining = 3000u;
                    draw_event_frame(base, event, &status);
                } else {
                    event_timer -= TICK_MS;
                }
            } else {
                if (event_remaining <= TICK_MS) {
                    event = EVENT_NONE;
                    event_timer = (uint16_t)(5000u + (uint16_t)(rng8() % 6u) * 1000u);
                    blink_timer = 4u * FRAME_INTERVAL_MS;
                    eyes = "EO";
                    draw_frame(base, eyes, "MC", &status);
                } else {
                    event_remaining -= TICK_MS;
                }
            }

            if (event == EVENT_NONE) {
                if (!blink.active) {
                    if (blink_timer <= TICK_MS) {
                        blink_start(&blink);
                        eyes = blink_eyes_for_step(&blink);
                        draw_frame(base, eyes, "MC", &status);
                        blink_timer = (4u + (rng8() % 8u)) * FRAME_INTERVAL_MS;
                    } else {
                        blink_timer -= TICK_MS;
                    }
                } else {
                    if (blink_tick(&blink, TICK_MS)) {
                        eyes = "EO";
                        draw_frame(base, eyes, "MC", &status);
                    } else {
                        eyes = blink_eyes_for_step(&blink);
                    }
                }
            }
        }

        // CLI disabled for smoother animation. Re-enable if needed.
        // char img1[11];
        // char img2[11];
        // build_raw_name(img1, "HU", "EO", "MC");
        // build_raw_name(img2, "HD", "EO", "MC");
        // card_reader_handle_cli(&status, img1, img2, TFT_WIDTH, TFT_HEIGHT);

        hal_delay_ms(TICK_MS);
    }
#endif
}

#ifdef ESP_PLATFORM
void app_main(void) {
    app_run();
}
#else
int main(void) {
    app_run();
    return 0;
}
#endif
