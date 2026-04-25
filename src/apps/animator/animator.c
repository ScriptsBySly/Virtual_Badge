#include "apps/animator/animator.h"

#include "drivers/card_reader/card_reader_api.h"
#include "hal/hal.h"
#include "system/render/render_api.h"

#include <stdio.h>

#ifndef TFT_WIDTH
#define TFT_WIDTH 128u
#endif

#ifndef TFT_HEIGHT
#define TFT_HEIGHT 160u
#endif

enum {
    FRAME_INTERVAL_MS = 500u,
    BLINK_STEP_MS = 500u,
    TICK_MS = 20u,
    BLINK_START_DELAY_MS = 2000u,
    BLINK_RANDOM_BASE_DELAY_MS = 2000u,
    BLINK_RANDOM_STEP_DELAY_MS = 500u,
    BLINK_RANDOM_STEP_COUNT = 8u,
    EVENT_DURATION_MS = 4000u,
    EVENT_RANDOM_BASE_DELAY_MS = 4000u,
    EVENT_RANDOM_STEP_DELAY_MS = 500u,
    EVENT_RANDOM_STEP_COUNT = 12u,
};

typedef enum {
    EVENT_NONE = 0,
    EVENT_HAPPY,
    EVENT_SADNEW,
    EVENT_MAD,
} event_type_t;

typedef struct {
    uint8_t active;
    uint8_t step;
    uint16_t remaining_ms;
    uint8_t mode;
} blink_state_t;

typedef struct pixel_head_states {
    const char *head;
    const char *eyes;
    const char *mouth;
} pixel_head_states_t;

static uint16_t g_animator_rng_state = 0xACE1u;

static uint16_t animator_ms_to_ticks(uint16_t duration_ms)
{
    return (uint16_t)(((duration_ms + (TICK_MS - 1u)) / TICK_MS) * TICK_MS);
}

static uint8_t animator_rng8(void)
{
    g_animator_rng_state = (uint16_t)((g_animator_rng_state >> 1) ^ (-(g_animator_rng_state & 1u) & 0xB400u));
    return (uint8_t)(g_animator_rng_state & 0xFFu);
}

static void animator_build_raw_name(char *out, const pixel_head_states_t *state)
{
    out[0] = state->head[0];
    out[1] = state->head[1];
    out[2] = state->eyes[0];
    out[3] = state->eyes[1];
    out[4] = state->mouth[0];
    out[5] = state->mouth[1];
    out[6] = '.';
    out[7] = 'R';
    out[8] = 'A';
    out[9] = 'W';
    out[10] = '\0';
}

static void animator_build_event_name(char *out, const char *base, event_type_t ev)
{
    out[0] = base[0];
    out[1] = base[1];
    if (ev == EVENT_HAPPY)
    {
        out[2] = 'H'; out[3] = 'A'; out[4] = 'P'; out[5] = 'P'; out[6] = 'Y';
        out[7] = '.'; out[8] = 'R'; out[9] = 'A'; out[10] = 'W'; out[11] = '\0';
        return;
    }
    if (ev == EVENT_SADNEW)
    {
        out[2] = 'S'; out[3] = 'A'; out[4] = 'D'; out[5] = 'N'; out[6] = 'E'; out[7] = 'W';
        out[8] = '.'; out[9] = 'R'; out[10] = 'A'; out[11] = 'W'; out[12] = '\0';
        return;
    }

    out[2] = 'M'; out[3] = 'A'; out[4] = 'D';
    out[5] = '.'; out[6] = 'R'; out[7] = 'A'; out[8] = 'W'; out[9] = '\0';
}

static void animator_draw_error_screen(const char *name)
{
    (void)render_show_text_screen("SD IMG FAIL", name, 0, 0);
}

static void animator_draw_sd_status_overlay(const card_reader_state_t *dev)
{
    char line0[CARD_READER_STATUS_LINE_CAPACITY] = {0};
    char line1[CARD_READER_STATUS_LINE_CAPACITY] = {0};
    char line2[CARD_READER_STATUS_LINE_CAPACITY] = {0};
    char line3[CARD_READER_STATUS_LINE_CAPACITY] = {0};

    card_reader_describe_status(dev, line0, line1, line2, line3);
    (void)render_show_text_screen(line0, line1, line2, line3);
}

static uint8_t animator_draw_frame(card_reader_state_t *dev,
                                   const pixel_head_states_t *state)
{
    char name[11];
    uint8_t ok = 0;

    animator_build_raw_name(name, state);
    printf("A:%s\n", name);
    (void)dev;
    ok = render_queue_raw565(name, TFT_WIDTH, TFT_HEIGHT);
    if (!ok)
    {
        animator_draw_error_screen(name);
    }
    return ok;
}

static uint8_t animator_draw_event_frame(card_reader_state_t *dev,
                                         const pixel_head_states_t *state,
                                         event_type_t ev)
{
    char name[13];
    uint8_t ok = 0;

    animator_build_event_name(name, state->head, ev);
    (void)dev;
    ok = render_queue_raw565(name, TFT_WIDTH, TFT_HEIGHT);
    if (!ok)
    {
        animator_draw_error_screen(name);
    }
    return ok;
}

static void animator_blink_start(blink_state_t *b)
{
    b->active = 1;
    b->step = 0;
    b->mode = animator_rng8() % 4u;
    
    if  (b->mode == 2)
    {
        b->remaining_ms = 1000u;
    }
    else if (b->mode == 3)
    {
        b->remaining_ms = 2000u + (uint16_t)(animator_rng8() % 7u) * 1000u;
    }
    else
    {
        b->remaining_ms = BLINK_STEP_MS;
    }
}

static const char *animator_blink_eyes_for_step(const blink_state_t *b)
{
    if (b->mode == 0)
    {
        return (b->step == 0) ? "EC" : "EO";
    }

    if (b->mode == 1)
    {
        if (b->step == 0) return "EM";
        if (b->step == 1) return "EC";
        return "EO";
    }

    if (b->mode == 2)
    {
        return (b->step == 0) ? "EM" : "EO";
    }

    return (b->step == 0) ? "EM" : "EO";
}

static uint8_t animator_blink_step_count(const blink_state_t *b)
{
    if (b->mode == 0) return 2;
    if (b->mode == 1) return 3;
    return 2;
}

static uint8_t animator_blink_tick(blink_state_t *b, uint16_t tick_ms)
{
    if (!b->active)
    {
        return 0;
    }
    if (b->remaining_ms > tick_ms)
    {
        b->remaining_ms -= tick_ms;
        return 0;
    }
    b->step++;
    if (b->step >= animator_blink_step_count(b))
    {
        b->active = 0;
        return 1;
    }
    b->remaining_ms = BLINK_STEP_MS;
    return 1;
}

/************************************************
* animator_random_event
* Chooses the next non-neutral animator event to display.
* Parameters: none.
* Returns: event id.
***************************************************/
static event_type_t animator_random_event(void)
{
    switch (animator_rng8() % 3u)
    {
        case 0:
            return EVENT_HAPPY;
        case 1:
            return EVENT_SADNEW;
        default:
            return EVENT_MAD;
    }
}

/************************************************
* animator_preload_secondary_event
* Loads the HU and HD frames for one event into the secondary cache.
* Parameters: ev = event whose images should be staged next.
* Returns: void.
***************************************************/
static void animator_preload_secondary_event(event_type_t ev)
{
    char hu_name[13] = {0};
    char hd_name[13] = {0};
    const char *names[2] = {0};

    if (ev == EVENT_NONE)
    {
        return;
    }

    animator_build_event_name(hu_name, "HU", ev);
    animator_build_event_name(hd_name, "HD", ev);
    names[0] = hu_name;
    names[1] = hd_name;
    (void)render_queue_preload_raw565_secondary_list(names, 2u, TFT_WIDTH, TFT_HEIGHT);
}

/************************************************
* animator_secondary_event_ready
* Reports whether both staged frames for one event are ready in the secondary cache.
* Parameters: ev = event whose HU and HD frames should be checked.
* Returns: 1 when both frames are ready, 0 otherwise.
***************************************************/
static uint8_t animator_secondary_event_ready(event_type_t ev)
{
    if (ev == EVENT_NONE)
    {
        return 0;
    }

    return render_secondary_preload_ready();
}

static void animator_show_wait_sd_status(const char *line0,
                                         const char *line1,
                                         const char *line2,
                                         const char *line3,
                                         void *ctx)
{
    (void)ctx;
    (void)render_show_text_screen(line0, line1, line2, line3);
}

/************************************************
* animator_preload_primary_frames
* Loads the normal animator face frames into the render primary cache.
* Parameters: none.
* Returns: void.
***************************************************/
static void animator_preload_primary_frames(void)
{
    static const char *const frame_names[] = {
        "HUEOMC.RAW",
        "HDEOMC.RAW",
        "HUEMMC.RAW",
        "HDEMMC.RAW",
        "HUECMC.RAW",
        "HDECMC.RAW",
        "HUHAPPY.RAW",
        "HDHAPPY.RAW",
        "HUSADNEW.RAW",
        "HDSADNEW.RAW",
        "HUMAD.RAW",
        "HDMAD.RAW",
    };
    uint8_t i = 0;

    /* Preload every animator image into the primary cache while secondary caching is disabled. */
    for (i = 0; i < (uint8_t)(sizeof(frame_names) / sizeof(frame_names[0])); i++)
    {
        (void)render_preload_raw565_primary(frame_names[i], TFT_WIDTH, TFT_HEIGHT);
    }
}

uint8_t animator_app_task(void *ctx)
{
    (void)ctx;
    printf("DBG:A4\n");

    /* Block here until the SD card is fully usable, showing wait status through render. */
    card_reader_state_t *dev = card_reader_wait_ready(animator_show_wait_sd_status, 0);
    /* Once storage is ready, give render access to the card reader for image loads. */
    render_bind_reader(dev);
    /* Fill the primary cache with the regular animator frames before the loop begins. */
    animator_preload_primary_frames();
    /* Show a brief SD status screen before the animation loop begins. */
    animator_draw_sd_status_overlay(dev);
    
    hal_delay_ms(2000);
    {
        /* Alternate between the two head images to create a simple bobbing animation. */
        const char *bases[2] = {"HU", "HD"};
        uint8_t base_index = 0;
        /* frame_timer controls when we swap HU/HD. */
        uint16_t frame_timer = FRAME_INTERVAL_MS;
        /* blink_timer decides when the next blink sequence should begin. */
        uint16_t blink_timer = animator_ms_to_ticks(BLINK_START_DELAY_MS);
        /* event_timer decides when the next temporary expression event begins. */
        uint16_t event_timer = animator_ms_to_ticks(EVENT_RANDOM_BASE_DELAY_MS);
        /* event_remaining tracks how long the current event frame should stay active. */
        uint16_t event_remaining = 0;
        /* current_event stores which temporary expression is active right now. */
        event_type_t current_event = EVENT_NONE;
        /* next_event stays staged in the secondary cache until it is time to play it. */
        event_type_t next_event = animator_random_event();
        /* blink holds the in-progress blink state machine, if any. */
        blink_state_t blink = {0};
        /* pixel_head_states holds the full face state used to build frame filenames. */
        pixel_head_states_t pixel_head_states = {
            .head = bases[base_index],
            .eyes = "EO",
            .mouth = "MC",
        };

        /* Stage the first event frames in the secondary cache before the main loop begins. */
        animator_preload_secondary_event(next_event);

        while (1)
        {
            if (dev)
            {

                /* Advance the base head frame at the configured frame cadence. */
                if (frame_timer <= TICK_MS)
                {
                    frame_timer = FRAME_INTERVAL_MS;
                    base_index ^= 1u;
                    pixel_head_states.head = bases[base_index];
                    /* Event frames follow the current head position while the temporary expression is active. */
                    if (current_event != EVENT_NONE)
                    {
                        animator_draw_event_frame(dev, &pixel_head_states, current_event);
                    }
                    else
                    {
                        animator_draw_frame(dev, &pixel_head_states);
                    }
                    continue;
                }
                else
                {
                    frame_timer -= TICK_MS;
                }

                /* Active events temporarily replace the neutral face until their timer expires. */
                if (current_event != EVENT_NONE)
                {
                    if (event_remaining <= TICK_MS)
                    {
                        current_event = EVENT_NONE;
                        event_remaining = 0;
                        /* Drop stale queued event frames before returning to the neutral animation. */
                        render_drop_pending_draws();
                        /* Once the current event is done, rotate the short-lived cache to the next event set. */
                        (void)render_queue_reset_secondary_cache();
                        next_event = animator_random_event();
                        animator_preload_secondary_event(next_event);
                        animator_draw_frame(dev, &pixel_head_states);
                    }
                    else
                    {
                        event_remaining -= TICK_MS;
                    }
                }
                /* No event is active, so count down until it is time to show a temporary expression. */
                else if (event_timer <= TICK_MS)
                {
                    /* Wait until render confirms both staged event frames are really in secondary cache. */
                    if (animator_secondary_event_ready(next_event))
                    {
                        printf("EV:S:%u\n", (unsigned)next_event);
                        /* Drop stale neutral frames so the event begins cleanly on its first image. */
                        render_drop_pending_draws();
                        current_event = next_event;
                        event_remaining = animator_ms_to_ticks(EVENT_DURATION_MS);
                        animator_draw_event_frame(dev, &pixel_head_states, current_event);
                        event_timer = animator_ms_to_ticks((uint16_t)(EVENT_RANDOM_BASE_DELAY_MS +
                            (uint16_t)(animator_rng8() % EVENT_RANDOM_STEP_COUNT) * EVENT_RANDOM_STEP_DELAY_MS));
                    }
                    else
                    {
                        printf("EV:W:%u\n", (unsigned)next_event);
                    }
                }
                else
                {
                    event_timer -= TICK_MS;
                }

                /* Blink logic runs first so the eye state is ready before any head-frame redraw. */
                if (current_event == EVENT_NONE && !blink.active)
                {
                    /* No blink is in progress, so count down until it is time to start one. */
                    if (blink_timer <= TICK_MS)
                    {
                        animator_blink_start(&blink);
                        pixel_head_states.eyes = animator_blink_eyes_for_step(&blink);
                        /* Redraw immediately so the blink starts on this tick. */
                        animator_draw_frame(dev, &pixel_head_states);
                        /* Randomize the wait until the next blink after this one completes. */
                        blink_timer = animator_ms_to_ticks((uint16_t)(BLINK_RANDOM_BASE_DELAY_MS +
                            (uint16_t)(animator_rng8() % BLINK_RANDOM_STEP_COUNT) * BLINK_RANDOM_STEP_DELAY_MS));
                    }
                    else
                    {
                        blink_timer -= TICK_MS;
                    }
                }
                /* A blink is already active, so advance its step machine. */
                else if (current_event == EVENT_NONE && animator_blink_tick(&blink, TICK_MS))
                {
                    /* Blink is still mid-sequence, so update the tracked eye state only. */
                    pixel_head_states.eyes = animator_blink_eyes_for_step(&blink);
                    /* When the blink finishes, redraw with open eyes. */
                    animator_draw_frame(dev, &pixel_head_states);
                }
            }

            /* Run the whole animation state machine at a fixed tick rate. */
            hal_delay_ms(TICK_MS);
        }
    }

    return 1;
}
