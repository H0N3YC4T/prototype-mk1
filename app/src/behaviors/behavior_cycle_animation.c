/* behavior_cycle_animation.c
 *
 * Nice!View theme + animation control behavior
 */

#define DT_DRV_COMPAT zmk_behavior_cycle_animation

#include <zephyr/kernel.h>

#include <zmk/event_manager.h>
#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/keymap.h>


#include <zmk/events/cycle_animation_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static const struct behavior_parameter_value_metadata param_values[] = {
    {
        .display_name = "Toggle Animation Movement",
        .value = NVC_TOGGLE,
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
    },
    {
        .display_name = "Go To Next Theme",
        .value = NVC_NEXT,
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
    },
};

static const struct behavior_parameter_metadata_set param_metadata_set[] = {{
    .param1_values = param_values,
    .param1_values_len = ARRAY_SIZE(param_values),
}};

static const struct behavior_parameter_metadata metadata = {
    .sets_len = ARRAY_SIZE(param_metadata_set),
    .sets = param_metadata_set,
};

#endif /* CONFIG_ZMK_BEHAVIOR_METADATA */

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event)
{
    struct cycle_animation_state_changed *evt =
        new_cycle_animation_state_changed();

    if (!evt) {
        return -ENOMEM;
    }

    switch (binding->param1) {
    case NVC_TOGGLE:
        evt->type = NVC_TOGGLE;
        break;

    case NVC_NEXT:
        evt->type = NVC_NEXT;
        break;

    default:
        LOG_ERR("Unknown animation command param1=%d", binding->param1);
        return -ENOTSUP;
    }

    return ZMK_EVENT_RAISE(*evt);
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event)
{
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_cycle_animation_driver_api = {
    .binding_pressed =  on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif
};

#define CYCLE_ANIMATION_INST(n)                                                 \
    BEHAVIOR_DT_INST_DEFINE(n,                                                  \
                            NULL,                                               \
                            NULL,                                               \
                            NULL,                                               \
                            NULL,                                               \
                            APPLICATION,                                        \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                \
                            &behavior_cycle_animation_driver_api);

DT_INST_FOREACH_STATUS_OKAY(CYCLE_ANIMATION_INST);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
