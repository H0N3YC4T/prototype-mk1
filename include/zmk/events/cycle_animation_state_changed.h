// SPDX-License-Identifier: MIT
#pragma once

#include <zephyr/kernel.h>
#include <zmk/event_manager.h>

#define NVC_TOGGLE 0
#define NVC_NEXT   1

struct cycle_animation_state_changed {
    int type;
};

ZMK_EVENT_DECLARE(cycle_animation_state_changed);