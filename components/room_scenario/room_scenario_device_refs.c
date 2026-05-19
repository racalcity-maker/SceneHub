#include "room_scenario.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    char (*device_ids)[ROOM_SCENARIO_DEVICE_ID_MAX_LEN];
    size_t max_count;
    size_t count;
    bool overflow;
} room_scenario_device_ref_collect_ctx_t;

static void room_scenario_emit_device_ref(const char *device_id,
                                          room_scenario_device_ref_cb_t cb,
                                          void *ctx)
{
    if (!device_id || !device_id[0] || !cb) {
        return;
    }
    cb(device_id, ctx);
}

void room_scenario_step_for_each_device_ref(const room_scenario_step_t *step,
                                            room_scenario_device_ref_cb_t cb,
                                            void *ctx)
{
    if (!step || !cb) {
        return;
    }
    switch (step->type) {
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND:
        room_scenario_emit_device_ref(step->data.device_command.device_id, cb, ctx);
        break;
    case ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT:
        room_scenario_emit_device_ref(step->data.wait_device_event.device_id, cb, ctx);
        break;
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP:
        for (uint8_t i = 0;
             i < step->data.device_command_group.command_count &&
             i < ROOM_SCENARIO_COMMAND_GROUP_MAX_COMMANDS;
             ++i) {
            room_scenario_emit_device_ref(step->data.device_command_group.commands[i].device_id,
                                          cb,
                                          ctx);
        }
        break;
    case ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT:
        for (uint8_t i = 0;
             i < step->data.wait_any_device_event.event_count &&
             i < ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS;
             ++i) {
            room_scenario_emit_device_ref(step->data.wait_any_device_event.events[i].device_id,
                                          cb,
                                          ctx);
        }
        break;
    case ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS:
        for (uint8_t i = 0;
             i < step->data.wait_all_device_events.event_count &&
             i < ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS;
             ++i) {
            room_scenario_emit_device_ref(step->data.wait_all_device_events.events[i].device_id,
                                          cb,
                                          ctx);
        }
        break;
    default:
        break;
    }
}

static void room_scenario_reactive_action_for_each_device_ref(
    const room_scenario_t *scenario,
    const room_scenario_reactive_action_t *action,
    room_scenario_device_ref_cb_t cb,
    void *ctx)
{
    if (!scenario || !action || !cb) {
        return;
    }
    if (action->type == ROOM_SCENARIO_STEP_DEVICE_COMMAND) {
        room_scenario_emit_device_ref(action->data.device_command.device_id, cb, ctx);
    }
    for (uint8_t i = 0;
         i < action->group_command_count &&
         i < ROOM_SCENARIO_MAX_REACTIVE_GROUP_COMMANDS;
         ++i) {
        size_t index = (size_t)action->group_command_start_index + i;
        if (index >= scenario->reactive_group_command_count ||
            index >= ROOM_SCENARIO_MAX_REACTIVE_GROUP_COMMANDS) {
            break;
        }
        room_scenario_emit_device_ref(scenario->reactive_group_commands[index].device_id,
                                      cb,
                                      ctx);
    }
}

static void room_scenario_branch_for_each_device_ref(const room_scenario_t *scenario,
                                                     const room_scenario_branch_t *branch,
                                                     room_scenario_device_ref_cb_t cb,
                                                     void *ctx)
{
    if (!scenario || !branch || !cb) {
        return;
    }
    if (branch->trigger.kind == ROOM_SCENARIO_REACTIVE_TRIGGER_DEVICE_EVENT) {
        room_scenario_emit_device_ref(branch->trigger.device_id, cb, ctx);
    }
    for (uint16_t i = 0;
         i < branch->on_complete_action_count &&
         i < ROOM_SCENARIO_MAX_REACTIVE_ACTIONS;
         ++i) {
        size_t index = (size_t)branch->on_complete_action_start_index + i;
        if (index >= scenario->reactive_action_count ||
            index >= ROOM_SCENARIO_MAX_REACTIVE_ACTIONS) {
            break;
        }
        room_scenario_reactive_action_for_each_device_ref(scenario,
                                                          &scenario->reactive_actions[index],
                                                          cb,
                                                          ctx);
    }
}

static bool room_scenario_device_ref_seen(const room_scenario_device_ref_collect_ctx_t *ctx,
                                          const char *device_id)
{
    if (!ctx || !device_id || !device_id[0]) {
        return true;
    }
    for (size_t i = 0; i < ctx->count && i < ctx->max_count; ++i) {
        if (strcmp(ctx->device_ids[i], device_id) == 0) {
            return true;
        }
    }
    return false;
}

static void room_scenario_collect_device_ref_cb(const char *device_id, void *ctx_ptr)
{
    room_scenario_device_ref_collect_ctx_t *ctx =
        (room_scenario_device_ref_collect_ctx_t *)ctx_ptr;
    if (!ctx || !ctx->device_ids || room_scenario_device_ref_seen(ctx, device_id)) {
        return;
    }
    if (ctx->count >= ctx->max_count) {
        ctx->overflow = true;
        return;
    }
    snprintf(ctx->device_ids[ctx->count],
             ROOM_SCENARIO_DEVICE_ID_MAX_LEN,
             "%s",
             device_id);
    ctx->count++;
}

esp_err_t room_scenario_collect_device_refs(
    const room_scenario_t *scenario,
    char out_device_ids[][ROOM_SCENARIO_DEVICE_ID_MAX_LEN],
    size_t max_device_ids,
    size_t *out_count)
{
    room_scenario_device_ref_collect_ctx_t ctx = {
        .device_ids = out_device_ids,
        .max_count = max_device_ids,
        .count = 0,
    };

    if (!scenario || !out_count || (max_device_ids > 0 && !out_device_ids)) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;
    for (size_t i = 0; i < scenario->step_count && i < ROOM_SCENARIO_MAX_STEPS; ++i) {
        room_scenario_step_for_each_device_ref(&scenario->steps[i],
                                               room_scenario_collect_device_ref_cb,
                                               &ctx);
    }
    for (size_t i = 0; i < scenario->reactive_action_count &&
                       i < ROOM_SCENARIO_MAX_REACTIVE_ACTIONS;
         ++i) {
        room_scenario_reactive_action_for_each_device_ref(
            scenario,
            &scenario->reactive_actions[i],
            room_scenario_collect_device_ref_cb,
            &ctx);
    }
    for (size_t i = 0; i < scenario->branch_count && i < ROOM_SCENARIO_MAX_BRANCHES; ++i) {
        room_scenario_branch_for_each_device_ref(scenario,
                                                 &scenario->branches[i],
                                                 room_scenario_collect_device_ref_cb,
                                                 &ctx);
    }
    *out_count = ctx.count;
    return scenario->step_count > ROOM_SCENARIO_MAX_STEPS ||
                   scenario->reactive_action_count > ROOM_SCENARIO_MAX_REACTIVE_ACTIONS ||
                   scenario->reactive_group_command_count > ROOM_SCENARIO_MAX_REACTIVE_GROUP_COMMANDS ||
                   scenario->branch_count > ROOM_SCENARIO_MAX_BRANCHES ||
                   ctx.overflow
               ? ESP_ERR_INVALID_SIZE
               : ESP_OK;
}
