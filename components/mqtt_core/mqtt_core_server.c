#include "mqtt_core_internal.h"

#include <errno.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

static const char *TAG = "mqtt_core";

#define MQTT_SWEEP_PERIOD_US (1000 * 1000)

static void configure_session_recv_timeout(mqtt_session_t *sess)
{
    if (!sess || sess->sock < 0) {
        return;
    }
    struct timeval tmo = {
        .tv_sec = (sess->keepalive > 0) ? 1 : 5,
        .tv_usec = 0,
    };
    setsockopt(sess->sock, SOL_SOCKET, SO_RCVTIMEO, &tmo, sizeof(tmo));
}

static bool read_connect_client_id_for_eviction(int sock, char *client_id, size_t client_id_len)
{
    uint8_t header = 0;
    int rem = 0;
    uint8_t *pkt = NULL;
    bool ok = false;

    if (!client_id || client_id_len == 0) {
        return false;
    }
    client_id[0] = '\0';
    if (recv_all(sock, &header, 1) != 1 ||
        (header >> 4) != 1 ||
        read_remaining_length(sock, &rem) < 0 ||
        rem > MQTT_MAX_PACKET) {
        return false;
    }
    pkt = heap_caps_malloc((size_t)rem + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pkt) {
        pkt = heap_caps_malloc((size_t)rem + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!pkt) {
        return false;
    }
    if (recv_all(sock, pkt, (size_t)rem) == rem) {
        pkt[rem] = 0;
        ok = mqtt_parse_connect_client_id(pkt, (size_t)rem, client_id, client_id_len) == 0;
    }
    heap_caps_free(pkt);
    return ok;
}

static bool evict_duplicate_client_from_full_pool(int sock)
{
    char client_id[CONFIG_STORE_CLIENT_ID_MAX] = {0};
    int old_sock = -1;
    bool evicted = false;

    if (!read_connect_client_id_for_eviction(sock, client_id, sizeof(client_id))) {
        return false;
    }

    lock();
    mqtt_session_t *old = find_session_by_client_id(client_id);
    if (old) {
        old->suppress_will = true;
        old_sock = request_session_prepare_close_locked(old, "duplicate client_id while full", 0);
        evicted = true;
    }
    unlock();
    request_session_close_socket(old_sock);
    if (evicted) {
        ESP_LOGW(TAG, "mqtt pool full; evicted duplicate client_id=%s", client_id);
    }
    return evicted;
}

static void handle_client(void *param)
{
    mqtt_session_t *sess = (mqtt_session_t *)param;
    uint8_t header = 0;
    size_t slot = session_index(sess);
    uint8_t *pkt = ensure_session_rx_buffer(slot);
    if (!pkt) {
        ESP_LOGE(TAG, "failed to allocate mqtt rx packet buffer for slot %u", (unsigned)slot);
        goto cleanup;
    }

    int rem = 0;
    if (recv_all(sess->sock, &header, 1) != 1 || read_remaining_length(sess->sock, &rem) < 0 || rem > MQTT_MAX_PACKET) {
        goto cleanup;
    }
    if (recv_all(sess->sock, pkt, rem) < 0) {
        goto cleanup;
    }
    pkt[rem] = 0;
    if ((header >> 4) != 1 || handle_connect(sess, pkt, rem) != 0) {
        send_connack(sess->sock, 0x02);
        goto cleanup;
    }
    configure_session_recv_timeout(sess);
    send_connack(sess->sock, 0x00);
    ESP_LOGI(TAG, "MQTT CONNECT %s keepalive=%u", sess->client_id, sess->keepalive);

    while (1) {
        int r = recv(sess->sock, &header, 1, 0);
        if (r <= 0) {
            int err = errno;
            if (sess->closing) {
                ESP_LOGW(TAG, "closing session %s", sess->client_id);
                break;
            }
            if (err == EAGAIN || err == EWOULDBLOCK) {
                mqtt_qos1_retry_due(sess);
                int64_t idle_ms = now_ms() - sess->last_rx_ms;
                int64_t limit_ms = (sess->keepalive > 0) ? (int64_t)sess->keepalive * 1500 : 60000;
                if (idle_ms >= limit_ms) {
                    ESP_LOGW(TAG, "keepalive timeout %s", sess->client_id);
                    break;
                }
                continue;
            }
            ESP_LOGW(TAG, "socket closed %s err=%d", sess->client_id, err);
            break;
        }
        if (read_remaining_length(sess->sock, &rem) < 0 || rem > MQTT_MAX_PACKET) {
            ESP_LOGW(TAG, "bad remaining length");
            break;
        }
        if (recv_all(sess->sock, pkt, rem) < 0) {
            break;
        }
        pkt[rem] = 0;
        sess->last_rx_ms = now_ms();
        uint8_t type = header >> 4;
        switch (type) {
        case 3:
            if (handle_publish(sess, header, pkt, rem) != 0) {
                ESP_LOGW(TAG, "publish parse fail");
                goto cleanup;
            }
            break;
        case 4:
            if (handle_puback(sess, pkt, rem) < 0) {
                ESP_LOGW(TAG, "PUBACK parse fail");
                goto cleanup;
            }
            break;
        case 8:
            if (handle_subscribe(sess, pkt, rem) < 0) {
                ESP_LOGW(TAG, "subscribe parse fail");
                goto cleanup;
            }
            break;
        case 10:
            if (handle_unsubscribe(sess, pkt, rem) < 0) {
                ESP_LOGW(TAG, "unsubscribe parse fail");
                goto cleanup;
            }
            break;
        case 12:
            send_pingresp(sess);
            break;
        case 14:
            sess->suppress_will = true;
            goto cleanup;
        default:
            ESP_LOGW(TAG, "unsupported packet type %u", type);
            goto cleanup;
        }
        mqtt_qos1_retry_due(sess);
        if (sess->keepalive > 0) {
            int64_t idle_ms = now_ms() - sess->last_rx_ms;
            if (idle_ms >= (int64_t)(sess->keepalive * 1500)) {
                ESP_LOGW(TAG, "keepalive timeout %s", sess->client_id);
                goto cleanup;
            }
        }
    }

cleanup:
    send_will_if_needed(sess);
    free_session(sess);
    vTaskDelete(NULL);
}

static void accept_task(void *param)
{
    (void)param;
    while (1) {
        struct sockaddr_in source_addr;
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(s_listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "accept failed: %d", errno);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        int ka = 1;
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &ka, sizeof(ka));
        struct timeval tmo = {.tv_sec = 5, .tv_usec = 0};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tmo, sizeof(tmo));
        struct timeval send_tmo = {.tv_sec = 2, .tv_usec = 0};
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &send_tmo, sizeof(send_tmo));

        lock();
        mqtt_session_t *sess = alloc_session();
        unlock();
        if (!sess) {
            if (!evict_duplicate_client_from_full_pool(sock)) {
                ESP_LOGW(TAG, "too many clients");
            }
            shutdown(sock, SHUT_RDWR);
            closesocket(sock);
            continue;
        }
        size_t slot = session_index(sess);
        if (slot >= MQTT_MAX_CLIENTS || !ensure_session_task_storage(slot)) {
            ESP_LOGE(TAG, "no memory for client task");
            shutdown(sock, SHUT_RDWR);
            closesocket(sock);
            free_session(sess);
            continue;
        }

        sess->sock = sock;
        TaskHandle_t task = xTaskCreateStatic(handle_client, "mqtt_client", MQTT_CLIENT_STACK, sess, 5,
                                              s_session_stacks[slot], s_session_tcbs[slot]);
        if (!task) {
            ESP_LOGE(TAG, "failed to start client task");
            shutdown(sock, SHUT_RDWR);
            closesocket(sock);
            free_session(sess);
            continue;
        }
        sess->task = task;
    }
}

esp_err_t mqtt_core_start_server(int port)
{
    s_listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (s_listen_sock < 0) {
        return ESP_FAIL;
    }

    int opt = 1;
    setsockopt(s_listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons((uint16_t)port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(s_listen_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "bind failed");
        closesocket(s_listen_sock);
        return ESP_FAIL;
    }
    if (listen(s_listen_sock, MQTT_MAX_CLIENTS) != 0) {
        ESP_LOGE(TAG, "listen failed");
        closesocket(s_listen_sock);
        return ESP_FAIL;
    }

    if (!s_sweep_timer) {
        const esp_timer_create_args_t args = {
            .callback = (esp_timer_cb_t)sweep_idle_sessions,
            .name = "mqtt_sweep",
        };
        esp_timer_create(&args, &s_sweep_timer);
        ESP_LOGI(TAG, "created mqtt sweep timer handle=%p", (void *)s_sweep_timer);
        esp_timer_start_periodic(s_sweep_timer, MQTT_SWEEP_PERIOD_US);
    }

    if (!ensure_accept_task_storage()) {
        ESP_LOGE(TAG, "failed to allocate accept task stack");
        closesocket(s_listen_sock);
        s_listen_sock = -1;
        return ESP_ERR_NO_MEM;
    }
    s_accept_task = xTaskCreateStatic(accept_task, "mqtt_accept", MQTT_ACCEPT_STACK, NULL, 5,
                                      s_accept_stack, s_accept_tcb);
    if (!s_accept_task) {
        ESP_LOGE(TAG, "failed to create accept task");
        closesocket(s_listen_sock);
        s_listen_sock = -1;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "MQTT broker started on %d", port);
    return ESP_OK;
}
