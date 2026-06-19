#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mqtt_core.h"
#include "mqtt_core_internal.h"

#define TEST_QUEUE_DEPTH          512
#define TEST_EVENT_WAIT_MS        200
#define TEST_STRESS_COUNT         40
#define TEST_PAR_TASKS            8
#define TEST_PAR_MESSAGES_PER_TASK 16

static QueueHandle_t s_event_queue;
static bool s_handler_registered;
static EXT_RAM_BSS_ATTR scenehub_event_t s_event_rx_scratch;
static EXT_RAM_BSS_ATTR scenehub_event_t s_event_build_scratch;
static EXT_RAM_BSS_ATTR mqtt_session_t s_session_scratch;
static EXT_RAM_BSS_ATTR char s_payload_scratch[MQTT_MAX_PAYLOAD + 32];

static void mqtt_test_event_handler(const scenehub_event_t *msg)
{
    if (!s_event_queue || !msg) {
        return;
    }
    xQueueSend(s_event_queue, msg, 0);
}

esp_err_t mqtt_core_test_init_helpers(void)
{
    if (!s_event_queue) {
        s_event_queue = xQueueCreateWithCaps(TEST_QUEUE_DEPTH,
                                             sizeof(scenehub_event_t),
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_event_queue) {
            s_event_queue = xQueueCreate(TEST_QUEUE_DEPTH, sizeof(scenehub_event_t));
        }
        if (!s_event_queue) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_handler_registered) {
        esp_err_t err = event_bus_register_handler(mqtt_test_event_handler);
        if (err != ESP_OK) {
            return err;
        }
        s_handler_registered = true;
    }
    return retain_init();
}

static void mqtt_test_flush_events(void)
{
    if (!s_event_queue) {
        return;
    }
    while (xQueueReceive(s_event_queue, &s_event_rx_scratch, 0) == pdTRUE) {
        // drain pending messages
    }
}

static void mqtt_test_clear_retain_table(void)
{
    if (!s_retain) {
        return;
    }
    for (size_t i = 0; i < MQTT_RETAIN_MAX; ++i) {
        s_retain[i].payload_len = 0;
        s_retain[i].payload[0] = '\0';
        s_retain[i].topic[0] = '\0';
        s_retain[i].qos = 0;
        s_retain[i].in_use = false;
    }
}

static retain_entry_t *mqtt_test_find_retain_entry(const char *topic)
{
    if (!s_retain || !topic) {
        return NULL;
    }
    for (size_t i = 0; i < MQTT_RETAIN_MAX; ++i) {
        if (s_retain[i].in_use && strcmp(s_retain[i].topic, topic) == 0) {
            return &s_retain[i];
        }
    }
    return NULL;
}

void setUp(void)
{
    mqtt_test_flush_events();
    mqtt_test_clear_retain_table();
    mqtt_qos1_clear_session_locked(&s_session_scratch);
    memset(&s_session_scratch, 0, sizeof(s_session_scratch));
}

void tearDown(void)
{
}

/* Basic mapping and event injection */

static void test_mqtt_topic_map(void)
{
    const char *topic = mqtt_core_topic_for_event(SCENEHUB_EVENT_AUDIO_PLAY);
    TEST_ASSERT_NOT_NULL(topic);
    TEST_ASSERT_EQUAL_STRING("audio/play", topic);
    TEST_ASSERT_NULL(mqtt_core_topic_for_event(SCENEHUB_EVENT_FLAG_CHANGED));
}

static void test_mqtt_client_stats_initial(void)
{
    mqtt_client_stats_t stats;
    mqtt_core_get_client_stats(&stats);
    TEST_ASSERT_EQUAL_UINT8(0, stats.total);
}

static void mqtt_test_expect_event(scenehub_event_type_t type,
                                   const char *topic,
                                   const char *payload)
{
    TEST_ASSERT_NOT_NULL(topic);
    TEST_ASSERT_NOT_NULL(payload);
    TEST_ASSERT_EQUAL(pdTRUE,
                      xQueueReceive(s_event_queue,
                                    &s_event_rx_scratch,
                                    pdMS_TO_TICKS(TEST_EVENT_WAIT_MS)));
    TEST_ASSERT_EQUAL(type, s_event_rx_scratch.type);
    TEST_ASSERT_EQUAL_STRING(topic, s_event_rx_scratch.topic);
    TEST_ASSERT_EQUAL_STRING(payload, s_event_rx_scratch.payload);
}

static void test_mqtt_inject_dispatch(void)
{
    const char *topic = "audio/play";
    const char *payload = "/sdcard/test.mp3";
    TEST_ASSERT_EQUAL(ESP_OK, mqtt_core_inject_message(topic, payload));
    mqtt_test_expect_event(SCENEHUB_EVENT_AUDIO_PLAY, topic, payload);
    mqtt_test_expect_event(SCENEHUB_EVENT_MQTT_MESSAGE, topic, payload);
}

static void test_mqtt_inject_preserves_long_topic(void)
{
    const char *topic = "quest/room_alpha/altar_controller/events/slot_01/uid_sequence/success";
    const char *payload = "ok";
    TEST_ASSERT_TRUE(strlen(topic) > 63);
    TEST_ASSERT_TRUE(strlen(topic) < sizeof(((scenehub_event_t *)0)->topic));

    TEST_ASSERT_EQUAL(ESP_OK, mqtt_core_inject_message(topic, payload));
    mqtt_test_expect_event(SCENEHUB_EVENT_MQTT_MESSAGE, topic, payload);
}

static void test_mqtt_inject_bounds_generic_payload(void)
{
    memset(s_payload_scratch, 'B', SCENEHUB_EVENT_TEXT_PAYLOAD_MAX_LEN + 16);
    s_payload_scratch[SCENEHUB_EVENT_TEXT_PAYLOAD_MAX_LEN + 16] = '\0';

    TEST_ASSERT_EQUAL(ESP_OK, mqtt_core_inject_message("misc/large", s_payload_scratch));
    TEST_ASSERT_EQUAL(pdTRUE,
                      xQueueReceive(s_event_queue,
                                    &s_event_rx_scratch,
                                    pdMS_TO_TICKS(TEST_EVENT_WAIT_MS)));
    TEST_ASSERT_EQUAL(SCENEHUB_EVENT_MQTT_MESSAGE, s_event_rx_scratch.type);
    TEST_ASSERT_EQUAL_STRING("misc/large", s_event_rx_scratch.topic);
    TEST_ASSERT_EQUAL_UINT32(SCENEHUB_EVENT_TEXT_PAYLOAD_MAX_LEN - 1,
                             strlen(s_event_rx_scratch.payload));
    TEST_ASSERT_EQUAL_CHAR('B', s_event_rx_scratch.payload[0]);
    TEST_ASSERT_EQUAL_CHAR('\0', s_event_rx_scratch.payload[SCENEHUB_EVENT_TEXT_PAYLOAD_MAX_LEN - 1]);
}

static void test_mqtt_bridge_ignores_inbound_mqtt_origin_events(void)
{
    scenehub_event_t *msg = &s_event_build_scratch;

    memset(msg, 0, sizeof(*msg));
    TEST_ASSERT_EQUAL(ESP_OK,
                      scenehub_event_make_text(msg,
                                               SCENEHUB_EVENT_AUDIO_PLAY,
                                               "audio/play",
                                               "/sdcard/test.mp3"));
    msg->origin = SCENEHUB_EVENT_ORIGIN_MQTT;
    TEST_ASSERT_FALSE(mqtt_core_should_bridge_event(msg));

    memset(msg, 0, sizeof(*msg));
    TEST_ASSERT_EQUAL(ESP_OK,
                      scenehub_event_make_text(msg,
                                               SCENEHUB_EVENT_MQTT_MESSAGE,
                                               "audio/play",
                                               "/sdcard/test.mp3"));
    msg->origin = SCENEHUB_EVENT_ORIGIN_MQTT;
    TEST_ASSERT_FALSE(mqtt_core_should_bridge_event(msg));
}

static void test_mqtt_bridge_allows_internal_and_web_origin_events(void)
{
    scenehub_event_t *msg = &s_event_build_scratch;

    memset(msg, 0, sizeof(*msg));
    TEST_ASSERT_EQUAL(ESP_OK,
                      scenehub_event_make_text(msg,
                                               SCENEHUB_EVENT_AUDIO_PLAY,
                                               NULL,
                                               "/sdcard/test.mp3"));
    TEST_ASSERT_TRUE(mqtt_core_should_bridge_event(msg));

    memset(msg, 0, sizeof(*msg));
    TEST_ASSERT_EQUAL(ESP_OK,
                      scenehub_event_make_text(msg,
                                               SCENEHUB_EVENT_WEB_COMMAND,
                                               "web/cmd",
                                               "{\"cmd\":\"reload\"}"));
    msg->origin = SCENEHUB_EVENT_ORIGIN_WEB;
    TEST_ASSERT_TRUE(mqtt_core_should_bridge_event(msg));
}

/* Protocol regression coverage */

static size_t mqtt_test_build_publish_body(uint8_t *buf,
                                           size_t buf_size,
                                           const char *topic,
                                           uint16_t pid,
                                           const char *payload)
{
    size_t topic_len = strlen(topic);
    size_t payload_len = strlen(payload);
    size_t total = 2 + topic_len + (pid ? 2 : 0) + payload_len;

    TEST_ASSERT_TRUE(total <= buf_size);
    buf[0] = (uint8_t)(topic_len >> 8);
    buf[1] = (uint8_t)(topic_len & 0xFF);
    memcpy(buf + 2, topic, topic_len);
    total = 2 + topic_len;
    if (pid) {
        buf[total++] = (uint8_t)(pid >> 8);
        buf[total++] = (uint8_t)(pid & 0xFF);
    }
    memcpy(buf + total, payload, payload_len);
    total += payload_len;
    return total;
}

static void test_mqtt_handle_publish_rejects_qos2(void)
{
    mqtt_session_t *sess = &s_session_scratch;
    uint8_t body[128] = {0};
    size_t len = 0;

    memset(sess, 0, sizeof(*sess));
    strcpy(sess->client_id, "relay");
    len = mqtt_test_build_publish_body(body, sizeof(body), "relay/cmd", 1, "{\"on\":true}");

    TEST_ASSERT_EQUAL(-1, handle_publish(sess, 0x34, body, len));
}

static void test_mqtt_handle_publish_acl_deny_qos1_returns_success(void)
{
    mqtt_session_t *sess = &s_session_scratch;
    uint8_t body[128] = {0};
    size_t len = 0;

    memset(sess, 0, sizeof(*sess));
    strcpy(sess->client_id, "relay");
    sess->sock = -1;
    len = mqtt_test_build_publish_body(body, sizeof(body), "web/cmd", 1, "{\"reload\":true}");

    TEST_ASSERT_EQUAL(0, handle_publish(sess, 0x32, body, len));
}

static void test_mqtt_effective_delivery_qos_uses_minimum(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, mqtt_effective_delivery_qos(0, 0));
    TEST_ASSERT_EQUAL_UINT8(0, mqtt_effective_delivery_qos(1, 0));
    TEST_ASSERT_EQUAL_UINT8(1, mqtt_effective_delivery_qos(1, 1));
    TEST_ASSERT_EQUAL_UINT8(1, mqtt_effective_delivery_qos(2, 1));
}

static void test_mqtt_next_packet_id_is_never_zero(void)
{
    for (size_t i = 0; i < 8; ++i) {
        TEST_ASSERT_NOT_EQUAL_UINT16(0, mqtt_next_packet_id());
    }
}

static void test_mqtt_puback_clears_matching_qos1_message(void)
{
    mqtt_session_t *sess = &s_session_scratch;
    uint8_t ack[2] = {0x12, 0x34};
    uint8_t *packet = heap_caps_malloc(4, MALLOC_CAP_8BIT);

    TEST_ASSERT_NOT_NULL(packet);
    sess->qos1_pending[0].in_use = true;
    sess->qos1_pending[0].packet_id = 0x1234;
    sess->qos1_pending[0].packet = packet;
    sess->qos1_pending[0].packet_len = 4;
    TEST_ASSERT_EQUAL_UINT32(1, mqtt_qos1_pending_count(sess));

    TEST_ASSERT_EQUAL(0, handle_puback(sess, ack, sizeof(ack)));
    TEST_ASSERT_EQUAL_UINT32(0, mqtt_qos1_pending_count(sess));
}

static void test_mqtt_puback_validation_and_unknown_id(void)
{
    mqtt_session_t *sess = &s_session_scratch;
    uint8_t zero_pid[2] = {0, 0};
    uint8_t unknown_pid[2] = {0, 7};

    TEST_ASSERT_EQUAL(-1, handle_puback(sess, zero_pid, sizeof(zero_pid)));
    TEST_ASSERT_EQUAL(-1, handle_puback(sess, unknown_pid, 1));
    TEST_ASSERT_EQUAL(0, handle_puback(sess, unknown_pid, sizeof(unknown_pid)));
}

static void test_mqtt_acl_requires_exact_client_id_match_and_default_deny(void)
{
    TEST_ASSERT_TRUE(acl_can_publish("relay", "relay/cmd"));
    TEST_ASSERT_FALSE(acl_can_publish("relayevil", "relay/cmd"));
    TEST_ASSERT_FALSE(acl_can_publish("unknown", "relay/cmd"));
    TEST_ASSERT_TRUE(acl_can_publish("relay", "cp/v1/dev/relay/status"));
    TEST_ASSERT_TRUE(acl_can_publish("relayevil", "cp/v1/dev/relayevil/status"));
    TEST_ASSERT_FALSE(acl_can_publish("relayevil", "cp/v1/dev/relay/status"));
    TEST_ASSERT_TRUE(acl_can_subscribe("relayevil", "cp/v1/dev/relayevil/control/command"));
    TEST_ASSERT_TRUE(acl_can_subscribe("relayevil", "cp/v1/dev/all/control/command"));
    TEST_ASSERT_TRUE(acl_can_publish("dcc-relay-room-2", "cp/v1/dev/relay_room_2/status"));
    TEST_ASSERT_TRUE(acl_can_subscribe("dcc-uid-gate-1", "cp/v1/dev/uid_gate_1/control/command"));
    TEST_ASSERT_FALSE(acl_can_publish("dcc-uid-gate-1", "cp/v1/dev/relay_room_2/status"));
}

static void test_mqtt_acl_identifies_static_service_clients(void)
{
    TEST_ASSERT_TRUE(acl_is_static_client_id("relay"));
    TEST_ASSERT_TRUE(acl_is_static_client_id("webui"));
    TEST_ASSERT_FALSE(acl_is_static_client_id("dcc-scenehub-node-s3"));
    TEST_ASSERT_FALSE(acl_is_static_client_id("scenehub_node_s3"));
    TEST_ASSERT_FALSE(acl_is_static_client_id(""));
}

static void test_mqtt_upsert_subscription_deduplicates_topic(void)
{
    mqtt_session_t *sess = &s_session_scratch;
    uint8_t granted_qos = 0xFF;

    memset(sess, 0, sizeof(*sess));
    TEST_ASSERT_TRUE(mqtt_upsert_subscription(sess, "relay/cmd", 0, &granted_qos));
    TEST_ASSERT_EQUAL_UINT8(0, granted_qos);
    TEST_ASSERT_EQUAL_UINT8(1, sess->sub_count);
    TEST_ASSERT_EQUAL_STRING("relay/cmd", sess->subs[0].topic);
    TEST_ASSERT_EQUAL_UINT8(0, sess->subs[0].qos);

    TEST_ASSERT_TRUE(mqtt_upsert_subscription(sess, "relay/cmd", 1, &granted_qos));
    TEST_ASSERT_EQUAL_UINT8(1, granted_qos);
    TEST_ASSERT_EQUAL_UINT8(1, sess->sub_count);
    TEST_ASSERT_EQUAL_UINT8(1, sess->subs[0].qos);
}

static void test_mqtt_upsert_subscription_enforces_slot_limit(void)
{
    mqtt_session_t *sess = &s_session_scratch;
    uint8_t granted_qos = 0xFF;
    char topic[MQTT_MAX_TOPIC];

    memset(sess, 0, sizeof(*sess));
    for (size_t i = 0; i < MQTT_MAX_SUBS; ++i) {
        snprintf(topic, sizeof(topic), "stress/sub/%u", (unsigned)i);
        TEST_ASSERT_TRUE(mqtt_upsert_subscription(sess, topic, (uint8_t)(i & 1), &granted_qos));
    }
    TEST_ASSERT_EQUAL_UINT32(MQTT_MAX_SUBS, sess->sub_count);

    TEST_ASSERT_FALSE(mqtt_upsert_subscription(sess, "stress/sub/overflow", 0, &granted_qos));
    TEST_ASSERT_EQUAL_UINT32(MQTT_MAX_SUBS, sess->sub_count);

    TEST_ASSERT_TRUE(mqtt_upsert_subscription(sess, "stress/sub/3", 1, &granted_qos));
    TEST_ASSERT_EQUAL_UINT32(MQTT_MAX_SUBS, sess->sub_count);
    TEST_ASSERT_EQUAL_UINT8(1, sess->subs[3].qos);
}

/* Stress coverage */

static void test_mqtt_inject_stress(void)
{
    const char *typed_topic = "audio/play";
    const char *generic_topic = "misc/topic";
    char payload[64];
    for (uint32_t i = 0; i < TEST_STRESS_COUNT; ++i) {
        snprintf(payload, sizeof(payload), "stress-%" PRIu32, i);
        const bool expect_typed = (i % 2) == 0;
        const char *topic = expect_typed ? typed_topic : generic_topic;
        TEST_ASSERT_EQUAL(ESP_OK, mqtt_core_inject_message(topic, payload));
        if (expect_typed) {
            mqtt_test_expect_event(SCENEHUB_EVENT_AUDIO_PLAY, topic, payload);
        }
        mqtt_test_expect_event(SCENEHUB_EVENT_MQTT_MESSAGE, topic, payload);
    }
    TEST_ASSERT_EQUAL(pdFALSE,
                      xQueueReceive(s_event_queue,
                                    &s_event_rx_scratch,
                                    pdMS_TO_TICKS(TEST_EVENT_WAIT_MS)));
}

typedef struct {
    uint32_t base;
    SemaphoreHandle_t done;
    esp_err_t err;
    uint32_t sent;
} mqtt_test_injector_params_t;

static uint32_t mqtt_test_typed_per_task(uint32_t base)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < TEST_PAR_MESSAGES_PER_TASK; ++i) {
        if (((i + base) % 2) == 0) {
            count++;
        }
    }
    return count;
}

static void mqtt_test_injector_task(void *arg)
{
    mqtt_test_injector_params_t *params = (mqtt_test_injector_params_t *)arg;
    const char *typed_topic = "audio/play";
    const char *generic_topic = "stress/topic";
    char payload[96];
    for (uint32_t i = 0; i < TEST_PAR_MESSAGES_PER_TASK; ++i) {
        snprintf(payload, sizeof(payload), "parallel-%" PRIu32 "-%" PRIu32, params->base, i);
        const bool typed = ((i + params->base) % 2) == 0;
        const char *topic = typed ? typed_topic : generic_topic;
        esp_err_t err = mqtt_core_inject_message(topic, payload);
        if (err != ESP_OK) {
            params->err = err;
            break;
        }
        params->sent++;
        taskYIELD();
    }
    xSemaphoreGive(params->done);
    vTaskDelete(NULL);
}

static void mqtt_test_drain_parallel_events(uint32_t total_messages, uint32_t typed_expected)
{
    uint32_t generic_seen = 0;
    uint32_t typed_seen = 0;
    while (generic_seen < total_messages || typed_seen < typed_expected) {
        TEST_ASSERT_EQUAL(pdTRUE,
                          xQueueReceive(s_event_queue,
                                        &s_event_rx_scratch,
                                        pdMS_TO_TICKS(TEST_EVENT_WAIT_MS)));
        if (s_event_rx_scratch.type == SCENEHUB_EVENT_AUDIO_PLAY) {
            typed_seen++;
        } else {
            TEST_ASSERT_EQUAL(SCENEHUB_EVENT_MQTT_MESSAGE, s_event_rx_scratch.type);
            generic_seen++;
        }
    }
    TEST_ASSERT_EQUAL(pdFALSE,
                      xQueueReceive(s_event_queue,
                                    &s_event_rx_scratch,
                                    pdMS_TO_TICKS(TEST_EVENT_WAIT_MS)));
}

static void test_mqtt_parallel_burst(void)
{
    const uint32_t total_messages = TEST_PAR_TASKS * TEST_PAR_MESSAGES_PER_TASK;
    SemaphoreHandle_t done_sem = xSemaphoreCreateCounting(TEST_PAR_TASKS, 0);
    TEST_ASSERT_NOT_NULL(done_sem);
    mqtt_test_injector_params_t params[TEST_PAR_TASKS];
    uint32_t typed_expected = 0;
    for (uint32_t i = 0; i < TEST_PAR_TASKS; ++i) {
        params[i].base = i * TEST_PAR_MESSAGES_PER_TASK;
        params[i].done = done_sem;
        params[i].err = ESP_OK;
        params[i].sent = 0;
        typed_expected += mqtt_test_typed_per_task(params[i].base);
        BaseType_t ok = xTaskCreate(mqtt_test_injector_task,
                                    "inj",
                                    3072,
                                    &params[i],
                                    tskIDLE_PRIORITY + 1,
                                    NULL);
        TEST_ASSERT_EQUAL(pdPASS, ok);
        vTaskDelay(1);
    }
    for (uint32_t i = 0; i < TEST_PAR_TASKS; ++i) {
        TEST_ASSERT_EQUAL(pdTRUE,
                          xSemaphoreTake(done_sem, pdMS_TO_TICKS(1000)));
    }
    for (uint32_t i = 0; i < TEST_PAR_TASKS; ++i) {
        TEST_ASSERT_EQUAL(ESP_OK, params[i].err);
        TEST_ASSERT_EQUAL_UINT32(TEST_PAR_MESSAGES_PER_TASK, params[i].sent);
    }
    vSemaphoreDelete(done_sem);
    mqtt_test_drain_parallel_events(total_messages, typed_expected);
}

/* Retain/topic helpers */

static void test_topic_matches_filter_wildcards(void)
{
    TEST_ASSERT_TRUE(topic_matches_filter("#", "alpha"));
    TEST_ASSERT_TRUE(topic_matches_filter("alpha/#", "alpha"));
    TEST_ASSERT_TRUE(topic_matches_filter("alpha/#", "alpha/beta"));
    TEST_ASSERT_TRUE(topic_matches_filter("alpha/+/gamma", "alpha/beta/gamma"));
    TEST_ASSERT_FALSE(topic_matches_filter("alpha/+/gamma", "alpha/beta/delta"));
    TEST_ASSERT_FALSE(topic_matches_filter("alpha/+", "alpha"));
    TEST_ASSERT_FALSE(topic_matches_filter("alpha/+", "alpha/beta/gamma"));
}

static void test_retain_empty_payload_clears_entry(void)
{
    retain_store("quest/retain", "value1", 0);
    retain_entry_t *slot = mqtt_test_find_retain_entry("quest/retain");
    TEST_ASSERT_NOT_NULL(slot);
    TEST_ASSERT_EQUAL_STRING("value1", slot->payload);

    retain_store("quest/retain", "", 0);
    TEST_ASSERT_NULL(mqtt_test_find_retain_entry("quest/retain"));
}

static void test_retain_table_capacity_and_reuse(void)
{
    char topic[MQTT_MAX_TOPIC];

    for (size_t i = 0; i < MQTT_RETAIN_MAX; ++i) {
        snprintf(topic, sizeof(topic), "quest/retain/%u", (unsigned)i);
        retain_store(topic, "value", 0);
        TEST_ASSERT_NOT_NULL(mqtt_test_find_retain_entry(topic));
    }

    retain_store("quest/retain/overflow", "value", 0);
    TEST_ASSERT_NULL(mqtt_test_find_retain_entry("quest/retain/overflow"));

    retain_store("quest/retain/3", "", 0);
    TEST_ASSERT_NULL(mqtt_test_find_retain_entry("quest/retain/3"));

    retain_store("quest/retain/reused", "value", 0);
    TEST_ASSERT_NOT_NULL(mqtt_test_find_retain_entry("quest/retain/reused"));
}

static void test_retain_payload_is_bounded_and_terminated(void)
{
    memset(s_payload_scratch, 'A', sizeof(s_payload_scratch));
    s_payload_scratch[sizeof(s_payload_scratch) - 1] = '\0';

    retain_store("quest/retain/big", s_payload_scratch, 1);

    retain_entry_t *slot = mqtt_test_find_retain_entry("quest/retain/big");
    TEST_ASSERT_NOT_NULL(slot);
    TEST_ASSERT_EQUAL_UINT32(MQTT_MAX_PAYLOAD - 1, slot->payload_len);
    TEST_ASSERT_EQUAL_CHAR('\0', slot->payload[MQTT_MAX_PAYLOAD - 1]);
    TEST_ASSERT_EQUAL_UINT8(1, slot->qos);
}

void register_mqtt_core_tests(void)
{
    RUN_TEST(test_mqtt_topic_map);
    RUN_TEST(test_mqtt_client_stats_initial);
    RUN_TEST(test_mqtt_inject_dispatch);
    RUN_TEST(test_mqtt_inject_preserves_long_topic);
    RUN_TEST(test_mqtt_inject_bounds_generic_payload);
    RUN_TEST(test_mqtt_bridge_ignores_inbound_mqtt_origin_events);
    RUN_TEST(test_mqtt_bridge_allows_internal_and_web_origin_events);
    RUN_TEST(test_mqtt_handle_publish_rejects_qos2);
    RUN_TEST(test_mqtt_handle_publish_acl_deny_qos1_returns_success);
    RUN_TEST(test_mqtt_effective_delivery_qos_uses_minimum);
    RUN_TEST(test_mqtt_next_packet_id_is_never_zero);
    RUN_TEST(test_mqtt_puback_clears_matching_qos1_message);
    RUN_TEST(test_mqtt_puback_validation_and_unknown_id);
    RUN_TEST(test_mqtt_acl_requires_exact_client_id_match_and_default_deny);
    RUN_TEST(test_mqtt_acl_identifies_static_service_clients);
    RUN_TEST(test_mqtt_upsert_subscription_deduplicates_topic);
    RUN_TEST(test_mqtt_upsert_subscription_enforces_slot_limit);
    RUN_TEST(test_mqtt_inject_stress);
    RUN_TEST(test_mqtt_parallel_burst);
    RUN_TEST(test_topic_matches_filter_wildcards);
    RUN_TEST(test_retain_empty_payload_clears_entry);
    RUN_TEST(test_retain_table_capacity_and_reuse);
    RUN_TEST(test_retain_payload_is_bounded_and_terminated);
}
