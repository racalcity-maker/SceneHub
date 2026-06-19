#include "mqtt_core_internal.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *client_id;
    const char *pub_prefix;
    const char *sub_prefix;
} acl_entry_t;

static const acl_entry_t k_acl[] = {
    {"pn532",  "access/", "access/"},
    {"laser",  "laser/",  "laser/"},
    {"relay",  "relay/",  "relay/"},
    {"puppet", "puppet/", "puppet/"},
    {"webui",  "web/",    "web/"},
};

static bool client_id_exact_match(const char *expected, const char *client_id)
{
    return expected && client_id && strcmp(expected, client_id) == 0;
}

static bool topic_prefix_match(const char *prefix, const char *topic)
{
    if (!prefix || !topic) {
        return false;
    }
    if (strcmp(prefix, "*") == 0) {
        return true;
    }
    size_t len = strlen(prefix);
    return strncmp(topic, prefix, len) == 0;
}

static bool valid_client_id(const char *client_id)
{
    return client_id && client_id[0];
}

static bool extract_contract_device_id(const char *topic,
                                       char *out_device_id,
                                       size_t out_device_id_size)
{
    const char *prefix = "cp/v1/dev/";
    const char *segment = NULL;
    const char *tail = NULL;
    size_t len = 0;

    if (!topic || !out_device_id || out_device_id_size == 0) {
        return false;
    }
    out_device_id[0] = '\0';
    if (strncmp(topic, prefix, strlen(prefix)) != 0) {
        return false;
    }
    segment = topic + strlen(prefix);
    tail = strchr(segment, '/');
    if (!tail) {
        return false;
    }
    len = (size_t)(tail - segment);
    if (len == 0 || len >= out_device_id_size) {
        return false;
    }
    memcpy(out_device_id, segment, len);
    out_device_id[len] = '\0';
    return true;
}

static bool client_id_maps_to_contract_device_id(const char *client_id, const char *device_id)
{
    const char *dcc_prefix = "dcc-";
    char normalized[MQTT_MAX_TOPIC] = {0};
    size_t out = 0;

    if (!valid_client_id(client_id) || !device_id || !device_id[0]) {
        return false;
    }
    if (strcmp(client_id, device_id) == 0) {
        return true;
    }
    if (strncmp(client_id, dcc_prefix, strlen(dcc_prefix)) != 0) {
        return false;
    }

    client_id += strlen(dcc_prefix);
    while (*client_id && out + 1 < sizeof(normalized)) {
        normalized[out++] = (*client_id == '-') ? '_' : *client_id;
        client_id++;
    }
    normalized[out] = '\0';
    return normalized[0] && strcmp(normalized, device_id) == 0;
}

static const acl_entry_t *find_static_acl_entry(const char *client_id)
{
    for (size_t i = 0; i < sizeof(k_acl) / sizeof(k_acl[0]); ++i) {
        if (client_id_exact_match(k_acl[i].client_id, client_id)) {
            return &k_acl[i];
        }
    }
    return NULL;
}

bool acl_is_static_client_id(const char *client_id)
{
    return find_static_acl_entry(client_id) != NULL;
}

static bool acl_can_access_self_contract(const char *client_id, const char *topic)
{
    char device_id[MQTT_MAX_TOPIC] = {0};

    if (!valid_client_id(client_id) || !topic) {
        return false;
    }
    if (!extract_contract_device_id(topic, device_id, sizeof(device_id))) {
        return false;
    }
    return client_id_maps_to_contract_device_id(client_id, device_id);
}

static bool acl_can_subscribe_broadcast_contract(const char *topic)
{
    return topic && strcmp(topic, "cp/v1/dev/all/control/command") == 0;
}

bool acl_can_publish(const char *client_id, const char *topic)
{
    const acl_entry_t *entry = NULL;

    if (acl_can_access_self_contract(client_id, topic)) {
        return true;
    }

    entry = find_static_acl_entry(client_id);
    if (entry) {
        return topic_prefix_match(entry->pub_prefix, topic);
    }

    return false;
}

bool acl_can_subscribe(const char *client_id, const char *topic)
{
    const acl_entry_t *entry = NULL;

    if (acl_can_access_self_contract(client_id, topic)) {
        return true;
    }
    if (acl_can_subscribe_broadcast_contract(topic)) {
        return valid_client_id(client_id);
    }

    entry = find_static_acl_entry(client_id);
    if (entry) {
        return topic_prefix_match(entry->sub_prefix, topic);
    }

    return false;
}

bool topic_matches_filter(const char *filter, const char *topic)
{
    if (!filter || !topic) {
        return false;
    }

    const char *f = filter;
    const char *t = topic;

    while (true) {
        size_t flen = 0;
        size_t tlen = 0;

        while (f[flen] && f[flen] != '/') {
            flen++;
        }
        while (t[tlen] && t[tlen] != '/') {
            tlen++;
        }

        if (flen == 1 && f[0] == '#') {
            return f[1] == '\0';
        }

        if (!(flen == 1 && f[0] == '+')) {
            if (flen != tlen || memcmp(f, t, flen) != 0) {
                return false;
            }
        }

        f += flen;
        t += tlen;

        if (*f == '\0' && *t == '\0') {
            return true;
        }
        if (*f == '/' && *t == '/') {
            ++f;
            ++t;
            continue;
        }
        if (*f == '/' && *t == '\0') {
            return (f[1] == '#' && f[2] == '\0');
        }
        return false;
    }
}
