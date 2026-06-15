#include "mqtt_core_internal.h"

#include <inttypes.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lwip/sockets.h"

static const char *TAG = "mqtt_core";
static size_t s_qos1_pending_bytes;
static int64_t s_qos1_last_pressure_log_ms;
static uint32_t s_qos1_suppressed_pressure_logs;

static size_t encode_remaining_length(uint8_t *out, size_t rem_len)
{
    size_t idx = 0;
    do {
        uint8_t byte = rem_len % 128;
        rem_len /= 128;
        if (rem_len > 0) {
            byte |= 0x80;
        }
        out[idx++] = byte;
    } while (rem_len > 0 && idx < 4);
    return idx;
}

int recv_all(int sock, uint8_t *buf, size_t len)
{
    size_t got = 0;
    while (got < len) {
        int r = recv(sock, buf + got, len - got, 0);
        if (r <= 0) {
            return -1;
        }
        got += (size_t)r;
    }
    return (int)got;
}

int send_all(int sock, const uint8_t *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        int r = send(sock, buf + sent, len - sent, 0);
        if (r <= 0) {
            return sent > 0 ? MQTT_SEND_PARTIAL_FAIL : MQTT_SEND_FAILED;
        }
        sent += (size_t)r;
    }
    return (int)sent;
}

static int send_all_session_locked(mqtt_session_t *sess, const uint8_t *buf, size_t len)
{
    if (!sess || sess->sock < 0 || !buf) {
        return -1;
    }
    if (sess->tx_lock) {
        xSemaphoreTake(sess->tx_lock, portMAX_DELAY);
    }
    int rc = send_all(sess->sock, buf, len);
    if (sess->tx_lock) {
        xSemaphoreGive(sess->tx_lock);
    }
    return rc;
}

int read_remaining_length(int sock, int *out_rem)
{
    int multiplier = 1;
    int value = 0;
    uint8_t encoded = 0;
    do {
        if (recv_all(sock, &encoded, 1) != 1) {
            return -1;
        }
        value += (encoded & 127) * multiplier;
        multiplier *= 128;
        if (multiplier > 128 * 128 * 128) {
            return -1;
        }
    } while ((encoded & 128) != 0);
    *out_rem = value;
    return 0;
}

int send_connack(int sock, uint8_t rc)
{
    uint8_t pkt[4] = {0x20, 0x02, 0x00, rc};
    return send_all(sock, pkt, sizeof(pkt));
}

int send_suback(mqtt_session_t *sess, uint16_t pid, uint8_t *qos, size_t count)
{
    if (!sess || !qos || count > MQTT_MAX_SUBS) {
        return -1;
    }

    uint8_t buf[1 + 4 + 2 + MQTT_MAX_SUBS];
    size_t rem_len = 2 + count;
    uint8_t rem_enc[4];
    size_t rem_enc_len = encode_remaining_length(rem_enc, rem_len);
    if (rem_enc_len == 0 || (1 + rem_enc_len + rem_len) > sizeof(buf)) {
        ESP_LOGW(TAG, "suback packet exceeds buffer (%zu)", count);
        return -1;
    }

    size_t idx = 0;
    buf[idx++] = 0x90;
    memcpy(&buf[idx], rem_enc, rem_enc_len);
    idx += rem_enc_len;
    buf[idx++] = (uint8_t)(pid >> 8);
    buf[idx++] = (uint8_t)(pid & 0xFF);
    for (size_t i = 0; i < count; ++i) {
        buf[idx++] = qos[i];
    }
    return send_all_session_locked(sess, buf, idx);
}

int send_puback(mqtt_session_t *sess, uint16_t pid)
{
    uint8_t buf[4] = {0x40, 0x02, (uint8_t)(pid >> 8), (uint8_t)(pid & 0xFF)};
    return send_all_session_locked(sess, buf, sizeof(buf));
}

int send_unsuback(mqtt_session_t *sess, uint16_t pid)
{
    uint8_t buf[4] = {0xB0, 0x02, (uint8_t)(pid >> 8), (uint8_t)(pid & 0xFF)};
    return send_all_session_locked(sess, buf, sizeof(buf));
}

int send_pingresp(mqtt_session_t *sess)
{
    uint8_t buf[2] = {0xD0, 0x00};
    return send_all_session_locked(sess, buf, sizeof(buf));
}

int send_publish_packet_to_sock(int sock,
                                const char *topic,
                                const char *payload,
                                uint8_t qos,
                                bool retain,
                                uint16_t pid)
{
    if (sock < 0 || !topic || !payload) {
        return -1;
    }
    size_t topic_len = strlen(topic);
    size_t payload_len = strlen(payload);
    if (topic_len > UINT16_MAX) {
        ESP_LOGW(TAG, "publish topic too long (%zu)", topic_len);
        return -1;
    }
    size_t rem_len = 2 + topic_len + payload_len + (qos ? 2 : 0);
    if (rem_len > MQTT_MAX_PACKET) {
        ESP_LOGW(TAG, "publish payload too large (%zu)", rem_len);
        return -1;
    }

    uint8_t fixed[1 + 4 + 2 + 2] = {0};
    uint8_t rem_enc[4] = {0};
    size_t rem_enc_len = encode_remaining_length(rem_enc, rem_len);
    size_t idx = 0;
    if (rem_enc_len == 0 || (1 + rem_enc_len + 2 + (qos ? 2 : 0)) > sizeof(fixed)) {
        ESP_LOGW(TAG, "publish fixed header exceeds buffer");
        return -1;
    }
    fixed[idx++] = 0x30 | (qos << 1) | (retain ? 0x01 : 0x00);
    memcpy(&fixed[idx], rem_enc, rem_enc_len);
    idx += rem_enc_len;
    fixed[idx++] = (uint8_t)(topic_len >> 8);
    fixed[idx++] = (uint8_t)(topic_len & 0xFF);
    if (send_all(sock, fixed, idx) < 0) {
        return -1;
    }
    if (topic_len > 0 && send_all(sock, (const uint8_t *)topic, topic_len) < 0) {
        return -1;
    }
    if (qos) {
        uint8_t pid_buf[2] = {
            (uint8_t)(pid >> 8),
            (uint8_t)(pid & 0xFF),
        };
        if (send_all(sock, pid_buf, sizeof(pid_buf)) < 0) {
            return -1;
        }
    }
    if (payload_len > 0 && send_all(sock, (const uint8_t *)payload, payload_len) < 0) {
        return -1;
    }
    return (int)(1 + rem_enc_len + rem_len);
}

static void log_qos1_pressure(const char *reason, const char *client_id)
{
    int64_t current_ms = now_ms();
    const char *name = client_id && client_id[0] ? client_id : "<unknown>";

    if (current_ms - s_qos1_last_pressure_log_ms < 5000) {
        s_qos1_suppressed_pressure_logs++;
        return;
    }

    uint32_t suppressed = s_qos1_suppressed_pressure_logs;
    s_qos1_suppressed_pressure_logs = 0;
    s_qos1_last_pressure_log_ms = current_ms;
    if (suppressed > 0) {
        ESP_LOGW(TAG, "QoS1 backpressure: %s for %s (suppressed %" PRIu32 " similar logs)",
                 reason,
                 name,
                 suppressed);
    } else {
        ESP_LOGW(TAG, "QoS1 backpressure: %s for %s", reason, name);
    }
}

static uint8_t *build_qos1_publish_packet(const char *topic,
                                          const char *payload,
                                          bool retain,
                                          uint16_t pid,
                                          size_t *out_len)
{
    size_t topic_len = strlen(topic);
    size_t payload_len = strlen(payload);
    size_t rem_len = 2 + topic_len + 2 + payload_len;
    uint8_t rem_enc[4] = {0};
    size_t rem_enc_len = encode_remaining_length(rem_enc, rem_len);
    size_t total_len = 1 + rem_enc_len + rem_len;
    uint8_t *packet = NULL;
    size_t idx = 0;

    if (!out_len || pid == 0 || topic_len > UINT16_MAX ||
        rem_len > MQTT_MAX_PACKET || total_len > MQTT_MAX_PACKET ||
        rem_enc_len == 0) {
        return NULL;
    }
    packet = heap_caps_malloc(total_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!packet) {
        packet = heap_caps_malloc(total_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!packet) {
        return NULL;
    }

    packet[idx++] = 0x32 | (retain ? 0x01 : 0x00);
    memcpy(packet + idx, rem_enc, rem_enc_len);
    idx += rem_enc_len;
    packet[idx++] = (uint8_t)(topic_len >> 8);
    packet[idx++] = (uint8_t)(topic_len & 0xFF);
    memcpy(packet + idx, topic, topic_len);
    idx += topic_len;
    packet[idx++] = (uint8_t)(pid >> 8);
    packet[idx++] = (uint8_t)(pid & 0xFF);
    memcpy(packet + idx, payload, payload_len);
    idx += payload_len;
    *out_len = idx;
    return packet;
}

static void mqtt_qos1_free_entry_locked(mqtt_qos1_pending_t *entry)
{
    if (!entry || !entry->in_use) {
        return;
    }
    if (entry->packet) {
        heap_caps_free(entry->packet);
    }
    if (s_qos1_pending_bytes >= entry->packet_len) {
        s_qos1_pending_bytes -= entry->packet_len;
    } else {
        s_qos1_pending_bytes = 0;
    }
    memset(entry, 0, sizeof(*entry));
}

void mqtt_qos1_clear_session_locked(mqtt_session_t *sess)
{
    if (!sess) {
        return;
    }
    for (size_t i = 0; i < MQTT_QOS1_MAX_INFLIGHT; ++i) {
        mqtt_qos1_free_entry_locked(&sess->qos1_pending[i]);
    }
}

size_t mqtt_qos1_pending_count(const mqtt_session_t *sess)
{
    size_t count = 0;
    if (!sess) {
        return 0;
    }
    for (size_t i = 0; i < MQTT_QOS1_MAX_INFLIGHT; ++i) {
        if (sess->qos1_pending[i].in_use) {
            count++;
        }
    }
    return count;
}

bool mqtt_qos1_handle_puback(mqtt_session_t *sess, uint16_t packet_id)
{
    bool found = false;
    if (!sess || packet_id == 0) {
        return false;
    }
    if (sess->tx_lock) {
        xSemaphoreTake(sess->tx_lock, portMAX_DELAY);
    }
    lock();
    for (size_t i = 0; i < MQTT_QOS1_MAX_INFLIGHT; ++i) {
        mqtt_qos1_pending_t *entry = &sess->qos1_pending[i];
        if (entry->in_use && entry->packet_id == packet_id) {
            mqtt_qos1_free_entry_locked(entry);
            found = true;
            break;
        }
    }
    unlock();
    if (sess->tx_lock) {
        xSemaphoreGive(sess->tx_lock);
    }
    return found;
}

static int send_qos1_publish_to_session_slot(size_t slot,
                                             int sock,
                                             const char *client_id,
                                             const char *topic,
                                             const char *payload,
                                             bool retain,
                                             uint16_t pid)
{
    mqtt_qos1_pending_t *reserved = NULL;
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    SemaphoreHandle_t tx_lock = NULL;

    packet = build_qos1_publish_packet(topic, payload, retain, pid, &packet_len);
    if (!packet) {
        ESP_LOGW(TAG, "QoS1 packet allocation failed");
        return -1;
    }

    mqtt_session_t *sess = NULL;
    lock();
    sess = (s_sessions && slot < MQTT_MAX_CLIENTS) ? &s_sessions[slot] : NULL;
    if (!sess || !sess->active || sess->closing || sess->sock != sock ||
        s_qos1_pending_bytes + packet_len > MQTT_QOS1_MAX_PENDING_BYTES) {
        unlock();
        heap_caps_free(packet);
        log_qos1_pressure("pending byte limit reached", client_id);
        return -1;
    }
    for (size_t i = 0; i < MQTT_QOS1_MAX_INFLIGHT; ++i) {
        if (!sess->qos1_pending[i].in_use) {
            reserved = &sess->qos1_pending[i];
            break;
        }
    }
    if (!reserved) {
        unlock();
        heap_caps_free(packet);
        log_qos1_pressure("inflight queue full; dropping outbound publish", client_id);
        return 0;
    }
    reserved->in_use = true;
    reserved->packet_id = pid;
    reserved->packet = packet;
    reserved->packet_len = packet_len;
    reserved->last_send_ms = now_ms();
    reserved->send_count = 0;
    s_qos1_pending_bytes += packet_len;
    tx_lock = sess->tx_lock;
    unlock();

    if (tx_lock) {
        xSemaphoreTake(tx_lock, portMAX_DELAY);
    }
    lock();
    sess = (s_sessions && slot < MQTT_MAX_CLIENTS) ? &s_sessions[slot] : NULL;
    if (!sess || !sess->active || sess->closing || sess->sock != sock ||
        !reserved->in_use || reserved->packet != packet) {
        unlock();
        if (tx_lock) {
            xSemaphoreGive(tx_lock);
        }
        return -1;
    }
    unlock();
    int rc = send_all(sock, packet, packet_len);
    if (rc >= 0) {
        lock();
        sess = (s_sessions && slot < MQTT_MAX_CLIENTS) ? &s_sessions[slot] : NULL;
        if (sess && sess->active && !sess->closing && sess->sock == sock &&
            reserved->in_use && reserved->packet == packet) {
            reserved->send_count = 1;
        }
        unlock();
    }
    if (tx_lock) {
        xSemaphoreGive(tx_lock);
    }
    if (rc == MQTT_SEND_PARTIAL_FAIL) {
        request_session_close_if_current(slot,
                                         sock,
                                         client_id,
                                         "QoS1 initial partial send",
                                         0);
        return rc;
    }
    if (rc < 0) {
        ESP_LOGW(TAG,
                 "QoS1 initial send deferred for %s pid=%u",
                 client_id && client_id[0] ? client_id : "<unknown>",
                 pid);
        return (int)packet_len;
    }
    return rc;
}

void mqtt_qos1_retry_due(mqtt_session_t *sess)
{
    size_t slot = session_index(sess);
    int sock = sess ? sess->sock : -1;
    int64_t current_ms = now_ms();

    if (!sess || slot >= MQTT_MAX_CLIENTS || sock < 0 || sess->closing) {
        return;
    }
    for (size_t i = 0; i < MQTT_QOS1_MAX_INFLIGHT; ++i) {
        SemaphoreHandle_t tx_lock = sess->tx_lock;
        if (tx_lock) {
            xSemaphoreTake(tx_lock, portMAX_DELAY);
        }
        lock();
        mqtt_qos1_pending_t *entry = &sess->qos1_pending[i];
        if (!sess->active || sess->closing || sess->sock != sock ||
            !entry->in_use ||
            current_ms - entry->last_send_ms < MQTT_QOS1_RETRY_MS) {
            unlock();
            if (tx_lock) {
                xSemaphoreGive(tx_lock);
            }
            continue;
        }
        entry->packet[0] |= 0x08;
        entry->last_send_ms = current_ms;
        uint16_t packet_id = entry->packet_id;
        uint32_t send_count = entry->send_count;
        uint32_t next_send_count = send_count + 1;
        uint8_t *packet = entry->packet;
        size_t packet_len = entry->packet_len;
        char client_id[CONFIG_STORE_CLIENT_ID_MAX] = {0};
        strncpy(client_id, sess->client_id, sizeof(client_id) - 1);
        if (send_count >= MQTT_QOS1_MAX_SEND_ATTEMPTS) {
            unlock();
            if (tx_lock) {
                xSemaphoreGive(tx_lock);
            }
            request_session_close_if_current(slot,
                                             sock,
                                             client_id,
                                             "QoS1 ack budget exhausted",
                                             0);
            return;
        }
        unlock();

        int rc = send_all(sock, packet, packet_len);
        if (rc == MQTT_SEND_PARTIAL_FAIL) {
            if (tx_lock) {
                xSemaphoreGive(tx_lock);
            }
            request_session_close_if_current(slot,
                                             sock,
                                             client_id,
                                             "QoS1 retry partial send",
                                             0);
            return;
        }
        if (rc == MQTT_SEND_FAILED) {
            if (tx_lock) {
                xSemaphoreGive(tx_lock);
            }
            ESP_LOGW(TAG,
                     "QoS1 retry send deferred for %s pid=%u attempt=%" PRIu32,
                     client_id[0] ? client_id : "<unknown>",
                     packet_id,
                     next_send_count);
            return;
        }
        lock();
        sess = (s_sessions && slot < MQTT_MAX_CLIENTS) ? &s_sessions[slot] : NULL;
        if (sess && sess->active && !sess->closing && sess->sock == sock &&
            entry->in_use && entry->packet == packet) {
            entry->send_count = next_send_count;
        }
        unlock();
        if (tx_lock) {
            xSemaphoreGive(tx_lock);
        }
        return;
    }
}

int send_publish_packet_to_session_slot(size_t slot,
                                        int sock,
                                        const char *client_id,
                                        const char *topic,
                                        const char *payload,
                                        uint8_t qos,
                                        bool retain,
                                        uint16_t pid)
{
    SemaphoreHandle_t tx_lock = NULL;
    if (!s_sessions || slot >= MQTT_MAX_CLIENTS || sock < 0) {
        return -1;
    }
    if (qos == 1) {
        return send_qos1_publish_to_session_slot(slot,
                                                sock,
                                                client_id,
                                                topic,
                                                payload,
                                                retain,
                                                pid);
    }
    lock();
    mqtt_session_t *sess = &s_sessions[slot];
    if (!sess->active || sess->closing || sess->sock != sock) {
        unlock();
        return -1;
    }
    tx_lock = sess->tx_lock;
    unlock();

    if (tx_lock) {
        xSemaphoreTake(tx_lock, portMAX_DELAY);
    }
    lock();
    sess = &s_sessions[slot];
    if (!sess->active || sess->closing || sess->sock != sock) {
        unlock();
        if (tx_lock) {
            xSemaphoreGive(tx_lock);
        }
        return -1;
    }
    unlock();

    int rc = send_publish_packet_to_sock(sock, topic, payload, qos, retain, pid);
    if (tx_lock) {
        xSemaphoreGive(tx_lock);
    }
    if (rc < 0) {
        request_session_close_if_current(slot, sock, client_id, "publish send failed", 0);
    }
    return rc;
}

int send_publish_packet(mqtt_session_t *sess, const char *topic, const char *payload, uint8_t qos, bool retain, uint16_t pid)
{
    if (!sess || !topic || !payload) {
        return -1;
    }
    size_t slot = session_index(sess);
    uint8_t *buf = ensure_session_tx_buffer(slot);
    if (!buf) {
        ESP_LOGE(TAG, "publish buffer alloc failed");
        return -1;
    }
    size_t topic_len = strlen(topic);
    size_t payload_len = strlen(payload);
    if (topic_len > UINT16_MAX) {
        ESP_LOGW(TAG, "publish topic too long (%zu)", topic_len);
        return -1;
    }
    size_t rem_len = 2 + topic_len + payload_len + (qos ? 2 : 0);
    if (rem_len > MQTT_MAX_PACKET) {
        ESP_LOGW(TAG, "publish payload too large (%zu)", rem_len);
        return -1;
    }

    uint8_t header = 0x30 | (qos << 1) | (retain ? 0x01 : 0x00);
    uint8_t rem_enc[4];
    size_t rem_enc_len = encode_remaining_length(rem_enc, rem_len);
    size_t total_len = 1 + rem_enc_len + rem_len;
    if (rem_enc_len == 0 || total_len > MQTT_MAX_PACKET) {
        ESP_LOGW(TAG, "publish packet exceeds buffer (topic=%zu payload=%zu total=%zu)", topic_len, payload_len, total_len);
        return -1;
    }

    size_t idx = 0;
    buf[idx++] = header;
    memcpy(&buf[idx], rem_enc, rem_enc_len);
    idx += rem_enc_len;
    buf[idx++] = (uint8_t)(topic_len >> 8);
    buf[idx++] = (uint8_t)(topic_len & 0xFF);
    memcpy(&buf[idx], topic, topic_len);
    idx += topic_len;
    if (qos) {
        buf[idx++] = (uint8_t)(pid >> 8);
        buf[idx++] = (uint8_t)(pid & 0xFF);
    }
    memcpy(&buf[idx], payload, payload_len);
    idx += payload_len;
    return send_all_session_locked(sess, buf, idx);
}
