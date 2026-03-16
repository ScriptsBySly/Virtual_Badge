#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#include <util/delay.h>

#include "card_reader.h"
#include "display.h"

#ifndef TFT_WIDTH
#define TFT_WIDTH 128u
#endif
#ifndef TFT_HEIGHT
#define TFT_HEIGHT 160u
#endif

#define FRAME_INTERVAL_MS 500u
#define BLINK_STEP_MS 120u
#define TICK_MS 20u

static uint16_t rng_state = 0xACE1u;

static uint8_t rng8(void) {
    rng_state = (uint16_t)((rng_state >> 1) ^ (-(rng_state & 1u) & 0xB400u));
    return (uint8_t)(rng_state & 0xFFu);
}

static void delay_ms(uint16_t ms) {
    while (ms--) {
        _delay_ms(1);
    }
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

static void draw_frame(const char *base, const char *eyes, const char *mouth) {
    char name[11];
    build_raw_name(name, base, eyes, mouth);
    (void)card_reader_draw_raw565(name, TFT_WIDTH, TFT_HEIGHT);
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

static void draw_event_frame(const char *base, event_type_t ev) {
    char name[13];
    build_event_name(name, base, ev);
    (void)card_reader_draw_raw565(name, TFT_WIDTH, TFT_HEIGHT);
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

int main(void) {
    display_init();
    display_fill_color(0x0000);

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
    draw_frame(base, eyes, "MC");
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
                    draw_event_frame(base, event);
                } else {
                    eyes = blink.active ? blink_eyes_for_step(&blink) : "EO";
                    draw_frame(base, eyes, "MC");
                }
            } else {
                frame_timer -= TICK_MS;
            }

            if (event == EVENT_NONE) {
                if (event_timer <= TICK_MS) {
                    event = (event_type_t)(1u + (rng8() % 3u));
                    event_remaining = 3000u;
                    draw_event_frame(base, event);
                } else {
                    event_timer -= TICK_MS;
                }
            } else {
                if (event_remaining <= TICK_MS) {
                    event = EVENT_NONE;
                    event_timer = (uint16_t)(5000u + (uint16_t)(rng8() % 6u) * 1000u);
                    blink_timer = 4u * FRAME_INTERVAL_MS;
                    eyes = "EO";
                    draw_frame(base, eyes, "MC");
                } else {
                    event_remaining -= TICK_MS;
                }
            }

            if (event == EVENT_NONE) {
                if (!blink.active) {
                    if (blink_timer <= TICK_MS) {
                        blink_start(&blink);
                        eyes = blink_eyes_for_step(&blink);
                        draw_frame(base, eyes, "MC");
                        blink_timer = (4u + (rng8() % 8u)) * FRAME_INTERVAL_MS;
                    } else {
                        blink_timer -= TICK_MS;
                    }
                } else {
                    if (blink_tick(&blink, TICK_MS)) {
                        eyes = "EO";
                        draw_frame(base, eyes, "MC");
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

        _delay_ms(TICK_MS);
    }
}
