#include "node_provisioning_config_api_internal.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "sdkconfig.h"

void *alloc_provisioning_buffer(size_t size)
{
    void *ptr = NULL;

    if (size == 0) {
        return NULL;
    }
#if CONFIG_SPIRAM
    ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr) {
        memset(ptr, 0, size);
        return ptr;
    }
#endif
    ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

bool ensure_provisioning_scratch(void)
{
    if (s_scratch) {
        return true;
    }

    s_scratch = (node_provisioning_scratch_t *)alloc_provisioning_buffer(sizeof(*s_scratch));
    if (!s_scratch) {
        ESP_LOGE(g_node_provisioning_tag,
                 "provisioning scratch alloc failed (%u bytes)",
                 (unsigned)sizeof(*s_scratch));
        return false;
    }
    return true;
}

bool ensure_config_json_buffer(void)
{
    if (s_config_json) {
        return true;
    }

    s_config_json = (char *)alloc_provisioning_buffer(NODE_PROVISIONING_CONFIG_JSON_CAPACITY);
    if (!s_config_json) {
        ESP_LOGE(g_node_provisioning_tag,
                 "config json buffer alloc failed (%u bytes)",
                 (unsigned)NODE_PROVISIONING_CONFIG_JSON_CAPACITY);
        return false;
    }
    return true;
}

bool ensure_post_body_buffer(void)
{
    if (s_post_body) {
        return true;
    }

    s_post_body = (char *)alloc_provisioning_buffer(NODE_PROVISIONING_POST_BODY_CAPACITY);
    if (!s_post_body) {
        ESP_LOGE(g_node_provisioning_tag,
                 "post body buffer alloc failed (%u bytes)",
                 (unsigned)NODE_PROVISIONING_POST_BODY_CAPACITY);
        return false;
    }
    return true;
}

void drain_request_body(httpd_req_t *req)
{
    char discard[256];
    int remaining = 0;

    if (!req || req->content_len <= 0) {
        return;
    }

    remaining = req->content_len;
    while (remaining > 0) {
        int chunk = remaining > (int)sizeof(discard) ? (int)sizeof(discard) : remaining;
        int received = httpd_req_recv(req, discard, chunk);
        if (received <= 0) {
            break;
        }
        remaining -= received;
    }
}

char *dup_request_json(const char *src, size_t len)
{
    char *copy = NULL;

    if (!src || len == 0 || len > NODE_RULE_BUNDLE_HTTP_MAX_LEN) {
        return NULL;
    }

    copy = (char *)alloc_provisioning_buffer(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, src, len);
    copy[len] = '\0';
    return copy;
}

bool read_request_body(httpd_req_t *req, char *out, size_t out_size)
{
    int remaining = 0;
    int offset = 0;

    if (!req || !out || out_size == 0 || req->content_len <= 0 || req->content_len >= (int)out_size) {
        return false;
    }

    remaining = req->content_len;
    while (remaining > 0) {
        int received = httpd_req_recv(req, out + offset, remaining);
        if (received <= 0) {
            return false;
        }
        offset += received;
        remaining -= received;
    }
    out[offset] = '\0';
    return true;
}

bool lock_post_body(void)
{
    if (!g_node_provisioning_post_body_mutex) {
        g_node_provisioning_post_body_mutex =
            xSemaphoreCreateMutexStatic(&g_node_provisioning_post_body_mutex_storage);
    }
    return g_node_provisioning_post_body_mutex &&
           xSemaphoreTake(g_node_provisioning_post_body_mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
}

void unlock_post_body(void)
{
    if (g_node_provisioning_post_body_mutex) {
        xSemaphoreGive(g_node_provisioning_post_body_mutex);
    }
}

bool lock_config_json(void)
{
    if (!g_node_provisioning_config_json_mutex) {
        g_node_provisioning_config_json_mutex =
            xSemaphoreCreateMutexStatic(&g_node_provisioning_config_json_mutex_storage);
    }
    return g_node_provisioning_config_json_mutex &&
           xSemaphoreTake(g_node_provisioning_config_json_mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
}

void unlock_config_json(void)
{
    if (g_node_provisioning_config_json_mutex) {
        xSemaphoreGive(g_node_provisioning_config_json_mutex);
    }
}
