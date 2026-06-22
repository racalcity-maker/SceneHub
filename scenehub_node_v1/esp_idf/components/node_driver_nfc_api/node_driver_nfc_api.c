#include "node_driver_nfc_api.h"

#include <string.h>

#include "node_driver_nfc_reader_runtime.h"

void node_driver_nfc_api_get_status(node_nfc_driver_status_t *out_status)
{
    node_nfc_reader_runtime_status_t runtime = {0};

    if (!out_status) {
        return;
    }

    memset(out_status, 0, sizeof(*out_status));
    node_driver_nfc_reader_runtime_get_status(&runtime);

    out_status->initialized = runtime.initialized;
    out_status->started = runtime.started;
    out_status->enabled = runtime.enabled;
    out_status->driver_ready = runtime.driver_ready;
    out_status->card_present = runtime.card_present;
    out_status->poll_interval_ms = runtime.poll_interval_ms;
    out_status->debounce_ms = runtime.debounce_ms;
    out_status->seen_count = runtime.seen_count;
    out_status->token_id = runtime.token_id;
    memcpy(out_status->reader_id, runtime.reader_id, sizeof(out_status->reader_id));
    memcpy(out_status->uid, runtime.uid, sizeof(out_status->uid));
    memcpy(out_status->last_seen_uid, runtime.last_seen_uid, sizeof(out_status->last_seen_uid));
    memcpy(out_status->health, runtime.health, sizeof(out_status->health));
    memcpy(out_status->state, runtime.state, sizeof(out_status->state));
    memcpy(out_status->error_code, runtime.error_code, sizeof(out_status->error_code));
}

esp_err_t node_driver_nfc_api_reinit(void)
{
    return node_driver_nfc_reader_runtime_reinit();
}

esp_err_t node_driver_nfc_api_reload(const node_config_t *node_config)
{
    return node_driver_nfc_reader_runtime_reload(node_config);
}
