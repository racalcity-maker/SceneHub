#include "mqtt_core_internal.h"

#include <string.h>

#include "esp_log.h"
#include "lwip/sockets.h"

static const char *TAG = "mqtt_core";

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
            return -1;
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
