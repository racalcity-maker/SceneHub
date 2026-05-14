#include "orchestrator_registry_internal.h"

#include <string.h>

#include "scenehub_command_result.h"

static const char *orch_runtime_badge(orch_runtime_state_t state)
{
    switch (state) {
    case ORCH_RUNTIME_STATE_ARMED:
        return "armed";
    case ORCH_RUNTIME_STATE_ACTIVE:
        return "active";
    case ORCH_RUNTIME_STATE_PAUSED:
        return "paused";
    case ORCH_RUNTIME_STATE_COMPLETED:
        return "completed";
    case ORCH_RUNTIME_STATE_TIMEOUT:
        return "timeout";
    case ORCH_RUNTIME_STATE_FAILED:
        return "failed";
    case ORCH_RUNTIME_STATE_IDLE:
    case ORCH_RUNTIME_STATE_UNKNOWN:
    default:
        return "";
    }
}

static const char *orch_health_badge(orch_health_t health)
{
    switch (health) {
    case ORCH_HEALTH_DEGRADED:
        return "degraded";
    case ORCH_HEALTH_FAULT:
        return "fault";
    case ORCH_HEALTH_OK:
    default:
        return "";
    }
}

static void orch_device_view_add_badge(orch_device_entry_t *dst, const char *badge)
{
    if (!dst || !badge || !badge[0] || dst->badge_count >= ORCH_REGISTRY_DEVICE_MAX_BADGES) {
        return;
    }
    quest_str_copy(dst->badges[dst->badge_count],
                   sizeof(dst->badges[dst->badge_count]),
                   badge);
    dst->badge_count++;
}

static void orch_device_view_finalize_badges(orch_device_entry_t *dst)
{
    if (!dst) {
        return;
    }
    quest_str_copy(dst->connectivity_text,
                   sizeof(dst->connectivity_text),
                   orch_connectivity_str(dst->connectivity));
    quest_str_copy(dst->health_text,
                   sizeof(dst->health_text),
                   orch_health_str(dst->health));
    quest_str_copy(dst->runtime_state_text,
                   sizeof(dst->runtime_state_text),
                   orch_runtime_state_str(dst->runtime_state));
    dst->badge_count = 0;
    orch_device_view_add_badge(dst, orch_runtime_badge(dst->runtime_state));
    orch_device_view_add_badge(dst, orch_health_badge(dst->health));
}

static const char *orch_observed_client_id(const quest_device_t *dev)
{
    if (!dev) {
        return "";
    }
    if (dev->client_id[0]) {
        return dev->client_id;
    }
    return dev->id;
}

static const char *orch_control_boot_id(const device_control_ingest_device_t *ingest)
{
    if (!ingest) {
        return "";
    }
    if (ingest->status_boot_id[0]) {
        return ingest->status_boot_id;
    }
    return ingest->heartbeat_boot_id;
}

static void orch_apply_control_ingest(const quest_device_t *dev, orch_device_entry_t *dst)
{
    device_control_ingest_device_t *ingest = NULL;
    uint64_t now_ms = orch_now_ms();
    if (!dev || !dst) {
        return;
    }
    ingest = orch_scratch_ingest();
    if (!ingest) {
        return;
    }
    if (device_control_ingest_get_device(orch_observed_client_id(dev), ingest) != ESP_OK) {
        dst->connectivity = ORCH_CONNECTIVITY_OFFLINE;
        dst->health = ORCH_HEALTH_FAULT;
        dst->has_fault = true;
        quest_str_copy(dst->state, sizeof(dst->state), "not observed");
        return;
    }

    dst->last_seen_ms = ingest->last_seen_ms;
    quest_str_copy(dst->fw_version, sizeof(dst->fw_version), ingest->status_fw_version);
    if (ingest->status_boot_id[0]) {
        quest_str_copy(dst->boot_id, sizeof(dst->boot_id), ingest->status_boot_id);
    } else {
        quest_str_copy(dst->boot_id, sizeof(dst->boot_id), ingest->heartbeat_boot_id);
    }
    quest_str_copy(dst->last_diag_code, sizeof(dst->last_diag_code), ingest->diag_code);
    quest_str_copy(dst->last_diag_message, sizeof(dst->last_diag_message), ingest->diag_message);
    quest_str_copy(dst->last_result_status, sizeof(dst->last_result_status), ingest->result_status);
    quest_str_copy(dst->last_result_error_code,
                sizeof(dst->last_result_error_code),
                ingest->result_error_code);

    dst->connectivity = device_control_ingest_is_online(ingest,
                                                         now_ms,
                                                         DEVICE_CONTROL_INGEST_DEFAULT_ONLINE_TIMEOUT_MS)
                            ? ORCH_CONNECTIVITY_ONLINE
                            : ORCH_CONNECTIVITY_OFFLINE;
    if (dst->connectivity == ORCH_CONNECTIVITY_OFFLINE) {
        dst->health = ORCH_HEALTH_FAULT;
        dst->has_fault = true;
        quest_str_copy(dst->state, sizeof(dst->state), "offline");
    }

    if (dst->connectivity != ORCH_CONNECTIVITY_OFFLINE &&
        ingest->has_status && ingest->status_state[0]) {
        quest_str_copy(dst->state, sizeof(dst->state), ingest->status_state);
    }
    if (ingest->has_status) {
        dst->has_runtime = ingest->status_runtime_active;
        dst->runtime_state = ingest->status_runtime_active ? ORCH_RUNTIME_STATE_ACTIVE : ORCH_RUNTIME_STATE_IDLE;
    }

    if (ingest->has_status) {
        orch_promote_health(dst, orch_health_from_status_text(ingest->status_health));
    }
    if (ingest->has_diag) {
        orch_promote_health(dst, orch_health_from_diag_level(ingest->diag_level));
    }
    if (ingest->has_result &&
        scenehub_command_result_is_failure(ingest->result_status)) {
        orch_promote_health(dst, ORCH_HEALTH_DEGRADED);
    }
}

void orch_device_view_fill_control_device(const device_control_ingest_device_t *ingest,
                                          orch_control_device_entry_t *dst)
{
    uint64_t now_ms = orch_now_ms();

    if (!ingest || !dst) {
        return;
    }
    memset(dst, 0, sizeof(*dst));
    quest_str_copy(dst->device_id, sizeof(dst->device_id), ingest->device_id);
    dst->connectivity = device_control_ingest_is_online(ingest,
                                                        now_ms,
                                                        DEVICE_CONTROL_INGEST_DEFAULT_ONLINE_TIMEOUT_MS)
                            ? ORCH_CONNECTIVITY_ONLINE
                            : ORCH_CONNECTIVITY_OFFLINE;
    dst->health = ORCH_HEALTH_OK;
    if (ingest->has_status) {
        dst->health = orch_health_from_status_text(ingest->status_health);
    }
    if (ingest->has_diag) {
        orch_health_t diag_health = orch_health_from_diag_level(ingest->diag_level);
        if (diag_health > dst->health) {
            dst->health = diag_health;
        }
    }
    if (dst->connectivity == ORCH_CONNECTIVITY_OFFLINE) {
        dst->health = ORCH_HEALTH_FAULT;
    }
    quest_str_copy(dst->connectivity_text,
                   sizeof(dst->connectivity_text),
                   orch_connectivity_str(dst->connectivity));
    quest_str_copy(dst->health_text,
                   sizeof(dst->health_text),
                   orch_health_str(dst->health));
    dst->last_seen_ms = ingest->last_seen_ms;
    quest_str_copy(dst->fw_version, sizeof(dst->fw_version), ingest->status_fw_version);
    quest_str_copy(dst->boot_id, sizeof(dst->boot_id), orch_control_boot_id(ingest));
    quest_str_copy(dst->mode, sizeof(dst->mode), ingest->status_mode);
    quest_str_copy(dst->state, sizeof(dst->state), ingest->status_state);
    dst->has_heartbeat = ingest->has_heartbeat;
    dst->has_status = ingest->has_status;
    dst->has_diag = ingest->has_diag;
    dst->has_result = ingest->has_result;
}

void orch_device_view_fill_device(const quest_device_t *dev,
                                  bool services_degraded,
                                  orch_device_entry_t *dst)
{
    if (!dev || !dst) {
        return;
    }
    memset(dst, 0, sizeof(*dst));
    quest_str_copy(dst->device_id, sizeof(dst->device_id), dev->id);
    quest_str_copy(dst->client_id, sizeof(dst->client_id), orch_observed_client_id(dev));
    quest_str_copy(dst->display_name, sizeof(dst->display_name), dev->name[0] ? dev->name : dev->id);
    quest_str_copy(dst->room_id, sizeof(dst->room_id), orch_default_room_id());
    dst->connectivity = ORCH_CONNECTIVITY_UNKNOWN;
    dst->health = ORCH_HEALTH_OK;
    dst->runtime_state = ORCH_RUNTIME_STATE_UNKNOWN;
    quest_str_copy(dst->state, sizeof(dst->state), "unknown");

    if (!dev->enabled) {
        dst->health = ORCH_HEALTH_DEGRADED;
        dst->has_degraded = true;
        quest_str_copy(dst->state, sizeof(dst->state), "disabled");
        orch_device_view_finalize_badges(dst);
        return;
    }

    orch_apply_control_ingest(dev, dst);

    if (dst->health != ORCH_HEALTH_FAULT && services_degraded) {
        dst->health = ORCH_HEALTH_DEGRADED;
        dst->has_degraded = true;
    }
    if (dst->health == ORCH_HEALTH_FAULT) {
        dst->has_fault = true;
    }
    orch_device_view_finalize_badges(dst);
}

esp_err_t orch_device_view_get_device(const orch_registry_snapshot_t *snapshot,
                                      const char *device_id,
                                      orch_device_entry_t *out)
{
    if (!snapshot || !device_id || !device_id[0] || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint8_t i = 0; i < snapshot->device_count; ++i) {
        if (strcmp(snapshot->devices[i].device_id, device_id) == 0) {
            *out = snapshot->devices[i];
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t orch_device_view_list_control_devices(orch_control_device_entry_t *out_devices,
                                                size_t max_devices,
                                                size_t *out_count)
{
    device_control_ingest_device_t *devices = NULL;
    size_t capacity = 0;
    size_t count = 0;
    esp_err_t err = ESP_OK;

    if (!out_devices || max_devices == 0 || !out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;
    err = orch_scratch_lock();
    if (err != ESP_OK) {
        return err;
    }
    devices = orch_scratch_ingest_devices(&capacity);
    if (!devices) {
        orch_scratch_unlock();
        return ESP_ERR_NO_MEM;
    }
    if (capacity > max_devices) {
        capacity = max_devices;
    }
    err = device_control_ingest_list_devices(devices, capacity, &count);
    if (err != ESP_OK) {
        orch_scratch_unlock();
        return err;
    }
    if (count > capacity) {
        count = capacity;
    }
    for (size_t i = 0; i < count; ++i) {
        orch_device_view_fill_control_device(&devices[i], &out_devices[i]);
    }
    *out_count = count;
    orch_scratch_unlock();
    return ESP_OK;
}
