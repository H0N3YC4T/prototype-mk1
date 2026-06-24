// SPDX-License-Identifier: MIT
#pragma once

#include <zephyr/kernel.h>
#include <zmk/event_manager.h>

#define NVC_PAUSE 0   // freeze on a random static frame of the current theme
#define NVC_NEXT  1   // forwards: next theme (resume play)
#define NVC_PREV  2   // backwards: previous theme (resume play)

struct cycle_animation_state_changed {
    int type;
};

ZMK_EVENT_DECLARE(cycle_animation_state_changed);