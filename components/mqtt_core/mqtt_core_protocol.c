#include "mqtt_core.h"
#include "mqtt_core_internal.h"

#include <string.h>

#include "esp_log.h"
#include "quest_device.h"

static const char *TAG = "mqtt_core";

#define MQTT_CONNACK_ACCEPTED             0x00
#define MQTT_CONNACK_IDENTIFIER_REJECTED  0x02
#define MQTT_CONNACK_SERVER_UNAVAILABLE   0x03
#define MQTT_CONNACK_BAD_USERNAME_PASSWORD 0x04
#define MQTT_CONNACK_NOT_AUTHORIZED       0x05
#define MQTT_DEVICE_CLIENT_ID_PREFIX      "dcc-"

typedef struct {
    size_t slot;
    int sock;
    uint8_t qos;
    uint16_t pid;
    char client_id[CONFIG_STORE_CLIENT_ID_MAX];
} publish_target_t;

static int parse_utf8_str(const uint8_t *buf, size_t len, size_t *offset, char *out, size_t out_len)
{
    if (*offset + 2 > len) {
        return -1;
    }
    uint16_t slen = (buf[*offset] << 8) | buf[*offset + 1];
    *offset += 2;
    if (*offset + slen > len || slen >= out_len) {
        return -1;
    }
    memcpy(out, buf + *offset, slen);
    out[slen] = 0;
    *offset += slen;
    return 0;
}

static bool mqtt_authenticate_client(const char *client_id, const char *username, const char *password)
{
    const app_config_t *cfg = config_store_get();
    if (!cfg) {
        return false;
    }
    if (cfg->mqtt.user_count == 0) {
        return true;
    }
    for (uint8_t i = 0; i < cfg->mqtt.user_count; ++i) {
        const app_mqtt_user_t *user = &cfg->mqtt.users[i];
        if (strcmp(user->client_id, client_id) != 0) {
            continue;
        }
        if (strcmp(user->username, username ? username : "") != 0) {
            continue;
        }
        if (strcmp(user->password, password ? password : "") != 0) {
            continue;
        }
        return true;
    }
    return false;
}

static bool mqtt_is_device_contract_client_id(const char *client_id)
{
    size_t prefix_len = strlen(MQTT_DEVICE_CLIENT_ID_PREFIX);
    if (!client_id || !client_id[0]) {
        return false;
    }
    if (strncmp(client_id, MQTT_DEVICE_CLIENT_ID_PREFIX, prefix_len) == 0 &&
        client_id[prefix_len] != '\0') {
        return true;
    }
    return !acl_is_static_client_id(client_id);
}

static size_t mqtt_active_device_client_count_locked(const char *exclude_client_id)
{
    size_t count = 0;

    if (!s_sessions) {
        return 0;
    }

    for (size_t i = 0; i < MQTT_MAX_CLIENTS; ++i) {
        const mqtt_session_t *candidate = &s_sessions[i];
        if (!candidate->active ||
            candidate->closing ||
            candidate->retiring ||
            !candidate->client_id[0]) {
            continue;
        }
        if (exclude_client_id && strcmp(candidate->client_id, exclude_client_id) == 0) {
            continue;
        }
        if (mqtt_is_device_contract_client_id(candidate->client_id)) {
            count++;
        }
    }
    return count;
}

static bool mqtt_device_client_capacity_available_locked(const char *client_id, const mqtt_session_t *replacement)
{
    if (!mqtt_is_device_contract_client_id(client_id)) {
        return true;
    }
    const char *exclude_client_id = replacement ? client_id : NULL;
    return mqtt_active_device_client_count_locked(exclude_client_id) < QUEST_DEVICE_MAX_DEVICES;
}

int mqtt_parse_connect_client_id(const uint8_t *buf,
                                 size_t len,
                                 char *client_id,
                                 size_t client_id_len)
{
    size_t off = 0;
    char proto[8];

    if (!buf || !client_id || client_id_len == 0) {
        return -1;
    }
    client_id[0] = '\0';
    if (parse_utf8_str(buf, len, &off, proto, sizeof(proto)) != 0) {
        return -1;
    }
    if (strcmp(proto, "MQTT") != 0 && strcmp(proto, "MQIsdp") != 0) {
        return -1;
    }
    if (off + 4 > len) {
        return -1;
    }
    off += 4;
    return parse_utf8_str(buf, len, &off, client_id, client_id_len);
}

bool mqtt_upsert_subscription(mqtt_session_t *sess,
                              const char *topic,
                              uint8_t requested_qos,
                              uint8_t *out_granted_qos)
{
    uint8_t granted_qos = requested_qos > 1 ? 1 : requested_qos;

    if (!sess || !topic || !topic[0]) {
        return false;
    }

    for (size_t i = 0; i < sess->sub_count; ++i) {
        if (strcmp(sess->subs[i].topic, topic) == 0) {
            sess->subs[i].qos = granted_qos;
            if (out_granted_qos) {
                *out_granted_qos = granted_qos;
            }
            return true;
        }
    }

    if (sess->sub_count >= MQTT_MAX_SUBS) {
        return false;
    }

    strncpy(sess->subs[sess->sub_count].topic, topic, sizeof(sess->subs[sess->sub_count].topic) - 1);
    sess->subs[sess->sub_count].topic[sizeof(sess->subs[sess->sub_count].topic) - 1] = '\0';
    sess->subs[sess->sub_count].qos = granted_qos;
    sess->sub_count++;
    if (out_granted_qos) {
        *out_granted_qos = granted_qos;
    }
    return true;
}

void publish_to_subscribers(const char *topic, const char *payload, uint8_t qos, bool retain_flag, mqtt_session_t *exclude)
{
    publish_target_t targets[MQTT_MAX_CLIENTS] = {0};
    size_t target_count = 0;
    if (!s_sessions || !s_lock) {
        ESP_LOGW(TAG, "publish ignored: mqtt core not initialized");
        return;
    }
    lock();
    if (retain_flag) {
        retain_store(topic, payload, qos);
    }
    for (size_t i = 0; i < MQTT_MAX_CLIENTS; ++i) {
        mqtt_session_t *s = &s_sessions[i];
        if (!s->active || s->closing || s->sock < 0 || s == exclude) {
            continue;
        }
        for (size_t j = 0; j < s->sub_count; ++j) {
            if (topic_matches_filter(s->subs[j].topic, topic)) {
                uint8_t out_qos = mqtt_effective_delivery_qos(qos, s->subs[j].qos);
                publish_target_t *target = &targets[target_count++];
                target->slot = i;
                target->sock = s->sock;
                target->qos = out_qos;
                target->pid = out_qos ? mqtt_next_packet_id() : 0;
                strncpy(target->client_id, s->client_id, sizeof(target->client_id) - 1);
                break;
            }
        }
    }
    unlock();

    for (size_t i = 0; i < target_count; ++i) {
        publish_target_t *target = &targets[i];
        (void)send_publish_packet_to_session_slot(target->slot,
                                                  target->sock,
                                                  target->client_id,
                                                  topic,
                                                  payload,
                                                  target->qos,
                                                  retain_flag,
                                                  target->pid);
    }
}

int handle_connect(mqtt_session_t *sess, const uint8_t *buf, size_t len)
{
    size_t off = 0;
    char proto[8];
    if (parse_utf8_str(buf, len, &off, proto, sizeof(proto)) != 0) {
        return MQTT_CONNACK_IDENTIFIER_REJECTED;
    }
    if (strcmp(proto, "MQTT") != 0 && strcmp(proto, "MQIsdp") != 0) {
        return MQTT_CONNACK_IDENTIFIER_REJECTED;
    }
    if (off + 4 > len) {
        return MQTT_CONNACK_IDENTIFIER_REJECTED;
    }
    uint8_t level = buf[off++];
    uint8_t flags = buf[off++];
    uint16_t keepalive = (buf[off] << 8) | buf[off + 1];
    off += 2;

    char client_id[CONFIG_STORE_CLIENT_ID_MAX];
    if (mqtt_parse_connect_client_id(buf, len, client_id, sizeof(client_id)) != 0) {
        return MQTT_CONNACK_IDENTIFIER_REJECTED;
    }
    off += 2 + strlen(client_id);

    if (level != 4) {
        ESP_LOGE(TAG, "Unsupported MQTT protocol level %u. Only 3.1.1 is supported.", level);
        return MQTT_CONNACK_IDENTIFIER_REJECTED;
    }

    int old_sock = -1;
    lock();
    mqtt_session_t *old = find_session_by_client_id(client_id);
    if (!mqtt_device_client_capacity_available_locked(client_id, old)) {
        unlock();
        ESP_LOGW(TAG,
                 "MQTT CONNECT rejected: device client capacity full client_id=%s limit=%u",
                 client_id,
                 (unsigned)QUEST_DEVICE_MAX_DEVICES);
        return MQTT_CONNACK_SERVER_UNAVAILABLE;
    }
    if (old && old != sess) {
        old->suppress_will = true;
        old_sock = request_session_prepare_close_locked(old, "duplicate client_id", 0);
    }
    strncpy(sess->client_id, client_id, sizeof(sess->client_id) - 1);
    unlock();
    request_session_close_socket(old_sock);

    sess->keepalive = keepalive;
    // strncpy убрали отсюда
    sess->last_rx_ms = now_ms();

    bool will_flag = flags & 0x04;
    bool will_retain = flags & 0x20;
    uint8_t will_qos = (flags >> 3) & 0x03;
    if (will_flag) {
        if (parse_utf8_str(buf, len, &off, sess->will.topic, sizeof(sess->will.topic)) != 0) {
            return MQTT_CONNACK_IDENTIFIER_REJECTED;
        }
        if (parse_utf8_str(buf, len, &off, sess->will.payload, sizeof(sess->will.payload)) != 0) {
            return MQTT_CONNACK_IDENTIFIER_REJECTED;
        }
        sess->will.has = true;
        sess->will.qos = will_qos;
        sess->will.retain = will_retain;
    }

    char username[CONFIG_STORE_USERNAME_MAX] = {0};
    char password[CONFIG_STORE_PASSWORD_MAX] = {0};
    if (flags & 0x80) {
        if (parse_utf8_str(buf, len, &off, username, sizeof(username)) != 0) {
            return MQTT_CONNACK_BAD_USERNAME_PASSWORD;
        }
    }
    if (flags & 0x40) {
        if (parse_utf8_str(buf, len, &off, password, sizeof(password)) != 0) {
            return MQTT_CONNACK_BAD_USERNAME_PASSWORD;
        }
    }
    if (!mqtt_authenticate_client(client_id, username, password)) {
        ESP_LOGW(TAG, "MQTT auth failed for client_id=%s", client_id);
        return MQTT_CONNACK_NOT_AUTHORIZED;
    }
    return MQTT_CONNACK_ACCEPTED;
}

int handle_subscribe(mqtt_session_t *sess, const uint8_t *buf, size_t len)
{
    size_t off = 0;
    if (len < 2) {
        return -1;
    }
    uint16_t pid = (buf[off] << 8) | buf[off + 1];
    off += 2;

    uint8_t granted[MQTT_MAX_SUBS];
    size_t granted_count = 0;

    while (off + 3 <= len && granted_count < MQTT_MAX_SUBS) {
        char topic[MQTT_MAX_TOPIC];
        if (parse_utf8_str(buf, len, &off, topic, sizeof(topic)) != 0) {
            return -1;
        }
        uint8_t rqos = buf[off++];
        if (!acl_can_subscribe(sess->client_id, topic)) {
            ESP_LOGW(TAG, "ACL deny sub %s -> %s", sess->client_id, topic);
            granted[granted_count++] = 0x80;
            continue;
        }
        if (mqtt_upsert_subscription(sess, topic, rqos, &granted[granted_count])) {
            uint8_t subscription_qos = granted[granted_count];
            granted_count++;
            deliver_retain(sess, topic, subscription_qos);
        } else {
            granted[granted_count++] = 0x80;
        }
    }

    return send_suback(sess, pid, granted, granted_count);
}

int handle_unsubscribe(mqtt_session_t *sess, const uint8_t *buf, size_t len)
{
    size_t off = 0;
    if (len < 2) {
        return -1;
    }
    uint16_t pid = (buf[off] << 8) | buf[off + 1];
    off += 2;

    while (off + 2 <= len) {
        char topic[MQTT_MAX_TOPIC];
        if (parse_utf8_str(buf, len, &off, topic, sizeof(topic)) != 0) {
            return -1;
        }

        for (size_t i = 0; i < sess->sub_count; ) {
            if (strcmp(sess->subs[i].topic, topic) == 0) {
                if (i + 1 < sess->sub_count) {
                    memmove(&sess->subs[i],
                            &sess->subs[i + 1],
                            (sess->sub_count - i - 1) * sizeof(sess->subs[0]));
                }
                memset(&sess->subs[sess->sub_count - 1], 0, sizeof(sess->subs[0]));
                sess->sub_count--;
                continue;
            }
            ++i;
        }
    }

    return send_unsuback(sess, pid);
}

int handle_publish(mqtt_session_t *sess, uint8_t header, uint8_t *buf, size_t len)
{
    size_t off = 0;
    if (len < 2) {
        return -1;
    }
    uint16_t topic_len = (buf[off] << 8) | buf[off + 1];
    off += 2;
    if (off + topic_len > len || topic_len >= MQTT_MAX_TOPIC) {
        return -1;
    }
    char topic[MQTT_MAX_TOPIC];
    memcpy(topic, buf + off, topic_len);
    topic[topic_len] = 0;
    off += topic_len;

    uint16_t pid = 0;
    uint8_t qos = (header >> 1) & 0x03;
    bool retain = header & 0x01;
    if (qos) {
        if (off + 2 > len) {
            return -1;
        }
        pid = (buf[off] << 8) | buf[off + 1];
        off += 2;
    }
    if (qos > 1) {
        ESP_LOGW(TAG, "unsupported publish qos=%u topic=%s", qos, topic);
        return -1;
    }
    if (qos == 1 && pid == 0) {
        ESP_LOGW(TAG, "invalid qos1 publish packet id=0 topic=%s", topic);
        return -1;
    }
    if (!acl_can_publish(sess->client_id, topic)) {
        ESP_LOGW(TAG, "ACL deny pub %s -> %s", sess->client_id, topic);
        if (qos == 1) {
            (void)send_puback(sess, pid);
        }
        return 0;
    }
    size_t payload_len = len - off;
    if (payload_len >= MQTT_MAX_PAYLOAD) {
        ESP_LOGW(TAG,
                 "publish payload too large client=%s topic=%s len=%u max=%u",
                 sess && sess->client_id[0] ? sess->client_id : "<unknown>",
                 topic,
                 (unsigned)payload_len,
                 (unsigned)(MQTT_MAX_PAYLOAD - 1));
        return -1;
    }
    buf[off + payload_len] = 0;
    char *payload = (char *)(buf + off);

    if (qos == 1 && send_puback(sess, pid) < 0) {
        return -1;
    }

    mqtt_core_inject_message(topic, payload);
    publish_to_subscribers(topic, payload, qos, retain, NULL);

    return 0;
}

int handle_puback(mqtt_session_t *sess, const uint8_t *buf, size_t len)
{
    if (!sess || !buf || len != 2) {
        return -1;
    }
    uint16_t packet_id = ((uint16_t)buf[0] << 8) | buf[1];
    if (packet_id == 0) {
        return -1;
    }
    if (!mqtt_qos1_handle_puback(sess, packet_id)) {
        ESP_LOGD(TAG, "PUBACK for completed/unknown packet id=%u client=%s",
                 packet_id,
                 sess->client_id[0] ? sess->client_id : "<unknown>");
    }
    return 0;
}
