#include "node_driver_pn532_i2c.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "node_driver_nfc_reader_runtime.h"
#include "sdkconfig.h"

static const char *TAG = "node_drv_pn532";

enum {
    NODE_PN532_I2C_FREQ_HZ = 100000,
    NODE_PN532_I2C_TIMEOUT_MS = 250,
    NODE_PN532_READY_POLL_MS = 5,
    NODE_PN532_READY_TIMEOUT_MS = 1000,
    NODE_PN532_FRAME_BUF_LEN = 64,
    NODE_PN532_UID_MAX_LEN = 10,
    NODE_PN532_INIT_RETRY_MIN_MS = 500,
    NODE_PN532_INIT_RETRY_MAX_MS = 8000,
    NODE_PN532_INIT_FAST_RETRY_LIMIT = 5,
    NODE_PN532_INIT_OFFLINE_RETRY_MS = 60000,
    NODE_PN532_INIT_RECOVERY_RETRY_MS = 15000,
    NODE_PN532_INIT_OFFLINE_LOG_INTERVAL_MS = 600000,
    NODE_PN532_INIT_RECOVERY_LOG_INTERVAL_MS = 60000,
    NODE_PN532_RECOVERY_BUS_THRESHOLD = 3,
    NODE_PN532_RECOVERY_HW_RESET_THRESHOLD = 6,
    NODE_PN532_RESET_LOW_MS = 10,
    NODE_PN532_RESET_RECOVER_MS = 40,
    NODE_PN532_LOG_INTERVAL_MS = 10000,
    NODE_PN532_MIN_POLL_INTERVAL_MS = 500,
    NODE_PN532_NOTIFY_REINIT = 1U << 0,
};

typedef struct {
    bool started;
    bool bus_ready;
    bool session_ready;
    bool ever_ready;
    bool poll_fail_debug_logged;
    bool absent_reported;
    uint32_t init_retry_delay_ms;
    uint32_t init_fail_count;
    uint32_t init_burst_fail_count;
    uint32_t poll_fail_count;
    uint32_t next_init_attempt_ms;
    bool pending_hw_reset;
    int last_init_err;
    int last_poll_err;
    uint32_t last_init_log_ms;
    uint32_t last_poll_log_ms;
    StaticTask_t task_storage;
    TaskHandle_t task_handle;
    uint8_t tx_buf[NODE_PN532_FRAME_BUF_LEN];
    uint8_t rx_buf[NODE_PN532_FRAME_BUF_LEN + 1U];
} node_pn532_i2c_state_t;

static node_pn532_i2c_state_t s_pn532;
static node_nfc_reader_config_t *s_pn532_config;
static StackType_t *s_pn532_task_stack_mem;
static i2c_master_bus_handle_t s_pn532_i2c_bus;
static i2c_master_dev_handle_t s_pn532_i2c_dev;

static void pn532_task(void *arg);

static void *alloc_pn532_buffer(size_t size)
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

static bool ensure_pn532_config(void)
{
    if (s_pn532_config) {
        return true;
    }

    s_pn532_config = (node_nfc_reader_config_t *)alloc_pn532_buffer(sizeof(*s_pn532_config));
    if (!s_pn532_config) {
        ESP_LOGE(TAG, "pn532 config alloc failed (%u bytes)", (unsigned)sizeof(*s_pn532_config));
        return false;
    }
    return true;
}

static StackType_t *allocate_pn532_task_stack(void)
{
    size_t stack_bytes = 3584U * sizeof(StackType_t);

    if (s_pn532_task_stack_mem) {
        return s_pn532_task_stack_mem;
    }
#if CONFIG_SPIRAM && CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
    s_pn532_task_stack_mem = (StackType_t *)heap_caps_malloc(stack_bytes,
                                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_pn532_task_stack_mem) {
        memset(s_pn532_task_stack_mem, 0, stack_bytes);
        ESP_LOGI(TAG, "pn532 task stack source=psram bytes=%u", (unsigned)stack_bytes);
        return s_pn532_task_stack_mem;
    }
    ESP_LOGW(TAG, "pn532 task stack psram alloc failed; using internal heap fallback");
#endif
    s_pn532_task_stack_mem = (StackType_t *)heap_caps_malloc(stack_bytes, MALLOC_CAP_8BIT);
    if (s_pn532_task_stack_mem) {
        memset(s_pn532_task_stack_mem, 0, stack_bytes);
        ESP_LOGI(TAG, "pn532 task stack source=internal_heap bytes=%u", (unsigned)stack_bytes);
    }
    return s_pn532_task_stack_mem;
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static int pn532_i2c_port(void)
{
    if (s_pn532_config && strcmp(s_pn532_config->bus, "i2c_1") == 0) {
        return 1;
    }
    return 0;
}

static bool text_present(const char *text)
{
    return text && text[0] != '\0';
}

static bool should_log_failure(uint32_t *last_log_ms, int *last_err, esp_err_t err, uint32_t interval_ms)
{
    uint32_t now_ms_value = now_ms();

    if (!last_log_ms || !last_err) {
        return true;
    }
    if (*last_err != (int)err) {
        *last_err = (int)err;
        *last_log_ms = now_ms_value;
        return true;
    }
    if ((now_ms_value - *last_log_ms) >= interval_ms) {
        *last_log_ms = now_ms_value;
        return true;
    }
    return false;
}

static void pn532_log_poll_failure_once(esp_err_t poll_err)
{
    uint8_t status = 0;
    uint8_t raw[24] = {0};
    esp_err_t status_err = ESP_OK;

    if (s_pn532.poll_fail_debug_logged || !s_pn532_i2c_dev) {
        return;
    }
    s_pn532.poll_fail_debug_logged = true;

    status_err = i2c_master_receive(s_pn532_i2c_dev, &status, 1, NODE_PN532_I2C_TIMEOUT_MS);
    ESP_LOGW(TAG,
             "pn532 poll miss err=%s status_err=%s status=0x%02x",
             esp_err_to_name(poll_err),
             esp_err_to_name(status_err),
             status);

    if (status_err == ESP_OK && status == 0x01) {
        esp_err_t raw_err = i2c_master_receive(s_pn532_i2c_dev, raw, sizeof(raw), NODE_PN532_I2C_TIMEOUT_MS);
        ESP_LOGW(TAG,
                 "pn532 poll pending raw err=%s bytes=%02x %02x %02x %02x %02x %02x %02x %02x",
                 esp_err_to_name(raw_err),
                 raw[0],
                 raw[1],
                 raw[2],
                 raw[3],
                 raw[4],
                 raw[5],
                 raw[6],
                 raw[7]);
    }
}

static bool reset_gpio_configured(void)
{
    return s_pn532_config && s_pn532_config->reset_gpio >= 0;
}

static void reset_failure_backoff(void)
{
    s_pn532.init_retry_delay_ms = NODE_PN532_INIT_RETRY_MIN_MS;
    s_pn532.init_fail_count = 0;
    s_pn532.init_burst_fail_count = 0;
    s_pn532.poll_fail_count = 0;
    s_pn532.next_init_attempt_ms = 0;
    s_pn532.pending_hw_reset = false;
    s_pn532.last_init_err = ESP_OK;
    s_pn532.last_poll_err = ESP_OK;
    s_pn532.last_init_log_ms = 0;
    s_pn532.last_poll_log_ms = 0;
}

static uint32_t clamp_delay_ms(uint32_t delay_ms)
{
    if (delay_ms == 0) {
        return 1;
    }
    return delay_ms;
}

static esp_err_t pn532_uninstall_bus(void)
{
    esp_err_t err = ESP_OK;

    if (s_pn532_i2c_dev) {
        err = i2c_master_bus_rm_device(s_pn532_i2c_dev);
        if (err != ESP_OK) {
            return err;
        }
        s_pn532_i2c_dev = NULL;
    }
    if (s_pn532_i2c_bus) {
        err = i2c_del_master_bus(s_pn532_i2c_bus);
        if (err != ESP_OK) {
            return err;
        }
        s_pn532_i2c_bus = NULL;
    }
    s_pn532.bus_ready = false;
    return ESP_OK;
}

static void pn532_reset_session(void)
{
    s_pn532.session_ready = false;
}

static void pn532_schedule_transport_recovery(bool reinstall_bus, bool hw_reset)
{
    pn532_reset_session();
    if (reinstall_bus) {
        if (pn532_uninstall_bus() != ESP_OK) {
            s_pn532.bus_ready = false;
        }
    }
    if (hw_reset && reset_gpio_configured()) {
        s_pn532.pending_hw_reset = true;
    }
}

static esp_err_t pn532_apply_hardware_reset(void)
{
    esp_err_t err = ESP_OK;

    if (!reset_gpio_configured()) {
        return ESP_ERR_INVALID_STATE;
    }
    err = gpio_set_direction((gpio_num_t)s_pn532_config->reset_gpio, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        return err;
    }
    err = gpio_set_level((gpio_num_t)s_pn532_config->reset_gpio, 0);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(NODE_PN532_RESET_LOW_MS));
    err = gpio_set_level((gpio_num_t)s_pn532_config->reset_gpio, 1);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(NODE_PN532_RESET_RECOVER_MS));
    return ESP_OK;
}

static esp_err_t pn532_apply_pending_recovery(void)
{
    esp_err_t err = ESP_OK;

    if (!s_pn532.pending_hw_reset) {
        return ESP_OK;
    }
    err = pn532_apply_hardware_reset();
    if (err == ESP_OK) {
        s_pn532.pending_hw_reset = false;
        ESP_LOGI(TAG, "pn532 hardware reset complete gpio=%d", s_pn532_config ? s_pn532_config->reset_gpio : -1);
    }
    return err;
}

static void schedule_init_retry(esp_err_t err)
{
    uint32_t wait_ms = s_pn532.init_retry_delay_ms;
    bool suspend = false;

    s_pn532.init_fail_count++;
    s_pn532.init_burst_fail_count++;

    if (s_pn532.ever_ready) {
        if (s_pn532.init_burst_fail_count >= NODE_PN532_RECOVERY_HW_RESET_THRESHOLD) {
            pn532_schedule_transport_recovery(true, true);
        } else if (s_pn532.init_burst_fail_count >= NODE_PN532_RECOVERY_BUS_THRESHOLD) {
            pn532_schedule_transport_recovery(true, false);
        }
    }

    if (s_pn532.init_burst_fail_count >= NODE_PN532_INIT_FAST_RETRY_LIMIT) {
        wait_ms = s_pn532.ever_ready ? NODE_PN532_INIT_RECOVERY_RETRY_MS
                                     : NODE_PN532_INIT_OFFLINE_RETRY_MS;
        suspend = true;
    }

    s_pn532.next_init_attempt_ms = now_ms() + clamp_delay_ms(wait_ms);
    if (suspend) {
        uint32_t log_interval_ms = s_pn532.ever_ready
                                       ? NODE_PN532_INIT_RECOVERY_LOG_INTERVAL_MS
                                       : NODE_PN532_INIT_OFFLINE_LOG_INTERVAL_MS;

        if (should_log_failure(&s_pn532.last_init_log_ms, &s_pn532.last_init_err, err, log_interval_ms)) {
            ESP_LOGW(TAG,
                     "pn532 init offline err=%s retry_in_ms=%lu failures=%lu",
                     esp_err_to_name(err),
                     (unsigned long)wait_ms,
                     (unsigned long)s_pn532.init_fail_count);
        }
    } else if (s_pn532.ever_ready &&
               should_log_failure(&s_pn532.last_init_log_ms,
                                  &s_pn532.last_init_err,
                                  err,
                                  NODE_PN532_LOG_INTERVAL_MS)) {
        ESP_LOGW(TAG,
                 "pn532 init pending err=%s retry_ms=%lu failures=%lu",
                 esp_err_to_name(err),
                 (unsigned long)wait_ms,
                 (unsigned long)s_pn532.init_fail_count);
    }

    if (suspend) {
        s_pn532.init_retry_delay_ms = NODE_PN532_INIT_RETRY_MIN_MS;
        s_pn532.init_burst_fail_count = 0;
        return;
    }

    if (s_pn532.init_retry_delay_ms < NODE_PN532_INIT_RETRY_MAX_MS) {
        s_pn532.init_retry_delay_ms *= 2U;
        if (s_pn532.init_retry_delay_ms > NODE_PN532_INIT_RETRY_MAX_MS) {
            s_pn532.init_retry_delay_ms = NODE_PN532_INIT_RETRY_MAX_MS;
        }
    }
}

static esp_err_t i2c_write_bytes(const uint8_t *data, size_t len)
{
    uint8_t wrapped[NODE_PN532_FRAME_BUF_LEN + 2U] = {0};

    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_pn532_i2c_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((len + 2U) > sizeof(wrapped)) {
        return ESP_ERR_INVALID_SIZE;
    }

    wrapped[0] = 0x00;
    memcpy(&wrapped[1], data, len);
    wrapped[len + 1U] = 0x00;
    return i2c_master_transmit(s_pn532_i2c_dev, wrapped, len + 2U, NODE_PN532_I2C_TIMEOUT_MS);
}

static esp_err_t i2c_read_chunk(uint8_t *out, size_t len)
{
    if (!out || len == 0 || (len + 1U) > sizeof(s_pn532.rx_buf)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_pn532_i2c_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = i2c_master_receive(s_pn532_i2c_dev,
                                       s_pn532.rx_buf,
                                       len + 1U,
                                       NODE_PN532_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    if (s_pn532.rx_buf[0] != 0x01) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    memcpy(out, &s_pn532.rx_buf[1], len);
    return ESP_OK;
}

static esp_err_t pn532_wait_ready(uint32_t timeout_ms)
{
    uint32_t elapsed_ms = 0;

    while (elapsed_ms < timeout_ms) {
        uint8_t status = 0;
        esp_err_t err = s_pn532_i2c_dev
                            ? i2c_master_receive(s_pn532_i2c_dev, &status, 1, NODE_PN532_I2C_TIMEOUT_MS)
                            : ESP_ERR_INVALID_STATE;
        if (err == ESP_OK && status == 0x01) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(NODE_PN532_READY_POLL_MS));
        elapsed_ms += NODE_PN532_READY_POLL_MS;
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t pn532_read_ack(void)
{
    static const uint8_t ack_frame[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
    uint8_t ack[sizeof(ack_frame)] = {0};
    esp_err_t err = pn532_wait_ready(NODE_PN532_READY_TIMEOUT_MS);

    if (err != ESP_OK) {
        return err;
    }
    err = i2c_read_chunk(ack, sizeof(ack));
    if (err != ESP_OK) {
        return err;
    }
    return memcmp(ack, ack_frame, sizeof(ack_frame)) == 0 ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t pn532_write_command(const uint8_t *payload, size_t payload_len)
{
    uint8_t checksum = 0;
    size_t frame_len = 0;
    size_t len_field = payload_len + 1U;

    if (!payload || payload_len == 0 || (payload_len + 8U) > sizeof(s_pn532.tx_buf)) {
        return ESP_ERR_INVALID_SIZE;
    }

    s_pn532.tx_buf[0] = 0x00;
    s_pn532.tx_buf[1] = 0x00;
    s_pn532.tx_buf[2] = 0xFF;
    s_pn532.tx_buf[3] = (uint8_t)len_field;
    s_pn532.tx_buf[4] = (uint8_t)(0U - s_pn532.tx_buf[3]);
    s_pn532.tx_buf[5] = 0xD4;
    checksum = 0xD4;

    for (size_t i = 0; i < payload_len; ++i) {
        s_pn532.tx_buf[6 + i] = payload[i];
        checksum = (uint8_t)(checksum + payload[i]);
    }
    s_pn532.tx_buf[6 + payload_len] = (uint8_t)(0U - checksum);
    frame_len = payload_len + 7U;

    return i2c_write_bytes(s_pn532.tx_buf, frame_len);
}

static esp_err_t pn532_read_response(uint8_t command,
                                     uint8_t *out_payload,
                                     size_t out_payload_cap,
                                     size_t *out_payload_len)
{
    uint8_t raw[NODE_PN532_FRAME_BUF_LEN + 1U] = {0};
    const uint8_t *frame = &raw[1];
    const uint8_t *tail = NULL;
    uint8_t len_field = 0;
    size_t payload_len = 0;
    size_t read_payload_cap = out_payload_cap;
    size_t read_len = 0;
    uint8_t checksum = 0;
    esp_err_t err = pn532_wait_ready(NODE_PN532_READY_TIMEOUT_MS);

    if (out_payload_len) {
        *out_payload_len = 0;
    }
    if (err != ESP_OK) {
        return err;
    }

    if (command == 0x02) {
        read_payload_cap = 4U;
    } else if (command == 0x14) {
        read_payload_cap = 0U;
    }
    read_len = 1U + 7U + read_payload_cap + 2U;
    if (read_len > sizeof(raw)) {
        return ESP_ERR_INVALID_SIZE;
    }

    err = s_pn532_i2c_dev
              ? i2c_master_receive(s_pn532_i2c_dev, raw, read_len, NODE_PN532_I2C_TIMEOUT_MS)
              : ESP_ERR_INVALID_STATE;
    if (err != ESP_OK) {
        return err;
    }
    if (raw[0] != 0x01) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (frame[0] != 0x00 || frame[1] != 0x00 || frame[2] != 0xFF) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    len_field = frame[3];
    if ((uint8_t)(frame[3] + frame[4]) != 0U || len_field < 2U) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (frame[5] != 0xD5 || frame[6] != (uint8_t)(command + 1U)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    payload_len = (size_t)len_field - 2U;
    if (payload_len > out_payload_cap || (7U + payload_len + 2U) > (read_len - 1U)) {
        return ESP_ERR_INVALID_SIZE;
    }
    tail = &frame[7];

    checksum = (uint8_t)(frame[5] + frame[6]);
    for (size_t i = 0; i < payload_len; ++i) {
        checksum = (uint8_t)(checksum + tail[i]);
    }
    if ((uint8_t)(checksum + tail[payload_len]) != 0U || tail[payload_len + 1U] != 0x00) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (payload_len > 0U && out_payload) {
        memcpy(out_payload, tail, payload_len);
    }
    if (out_payload_len) {
        *out_payload_len = payload_len;
    }
    return ESP_OK;
}

static esp_err_t pn532_run_command(uint8_t command,
                                   const uint8_t *payload,
                                   size_t payload_len,
                                   uint8_t *out_payload,
                                   size_t out_payload_cap,
                                   size_t *out_payload_len)
{
    uint8_t cmd_buf[16] = {0};
    esp_err_t err = ESP_OK;

    if ((payload_len + 1U) > sizeof(cmd_buf)) {
        return ESP_ERR_INVALID_SIZE;
    }
    cmd_buf[0] = command;
    if (payload_len > 0U && payload) {
        memcpy(&cmd_buf[1], payload, payload_len);
    }

    err = pn532_write_command(cmd_buf, payload_len + 1U);
    if (err != ESP_OK) {
        return err;
    }
    err = pn532_read_ack();
    if (err != ESP_OK) {
        return err;
    }
    return pn532_read_response(command, out_payload, out_payload_cap, out_payload_len);
}

static esp_err_t pn532_install_bus(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = pn532_i2c_port(),
        .sda_io_num = s_pn532_config->i2c_sda_gpio,
        .scl_io_num = s_pn532_config->i2c_scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = s_pn532_config->i2c_address,
        .scl_speed_hz = NODE_PN532_I2C_FREQ_HZ,
        .scl_wait_us = 200000,
    };
    esp_err_t err = ESP_OK;

    if (s_pn532_i2c_dev) {
        s_pn532.bus_ready = true;
        return ESP_OK;
    }

    err = i2c_new_master_bus(&bus_cfg, &s_pn532_i2c_bus);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_master_bus_add_device(s_pn532_i2c_bus, &dev_cfg, &s_pn532_i2c_dev);
    if (err == ESP_OK) {
        s_pn532.bus_ready = true;
    }
    return err;
}

static esp_err_t pn532_init_session(void)
{
    uint8_t payload[24] = {0};
    uint8_t fw_payload[24] = {0};
    size_t fw_payload_len = 0;
    size_t payload_len = 0;
    static const uint8_t sam_cfg[] = {0x01, 0x14, 0x01};
    esp_err_t err = ESP_OK;

    err = pn532_apply_pending_recovery();
    if (err != ESP_OK) {
        return err;
    }

    if (!s_pn532.bus_ready) {
        err = pn532_install_bus();
        if (err != ESP_OK) {
            return err;
        }
    }
    err = pn532_run_command(0x02, NULL, 0, fw_payload, sizeof(fw_payload), &fw_payload_len);
    if (err != ESP_OK) {
        return err;
    }
    if (fw_payload_len < 4U) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    err = pn532_run_command(0x14, sam_cfg, sizeof(sam_cfg), payload, sizeof(payload), &payload_len);
    if (err != ESP_OK) {
        return err;
    }
    s_pn532.session_ready = true;
    s_pn532.ever_ready = true;
    if (s_pn532.init_fail_count > 0) {
        ESP_LOGI(TAG,
                 "pn532 recovered after %lu init failure(s)",
                 (unsigned long)s_pn532.init_fail_count);
    }
    s_pn532.init_retry_delay_ms = NODE_PN532_INIT_RETRY_MIN_MS;
    s_pn532.init_fail_count = 0;
    s_pn532.init_burst_fail_count = 0;
    s_pn532.next_init_attempt_ms = 0;
    s_pn532.pending_hw_reset = false;
    s_pn532.poll_fail_debug_logged = false;
    s_pn532.absent_reported = false;
    s_pn532.last_init_err = ESP_OK;
    s_pn532.last_init_log_ms = 0;
    ESP_LOGI(TAG,
             "pn532 ready fw=%u.%u support=0x%02x ic=0x%02x",
             (unsigned)fw_payload[1],
             (unsigned)fw_payload[2],
             (unsigned)fw_payload[3],
             (unsigned)fw_payload[0]);
    return ESP_OK;
}

static void uid_to_text(const uint8_t *uid, size_t uid_len, char *out, size_t out_size)
{
    size_t offset = 0;

    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!uid || uid_len == 0) {
        return;
    }
    for (size_t i = 0; i < uid_len && (offset + 2U) < out_size; ++i) {
        int written = snprintf(out + offset, out_size - offset, "%02X", uid[i]);

        if (written <= 0 || (size_t)written >= (out_size - offset)) {
            out[0] = '\0';
            return;
        }
        offset += (size_t)written;
    }
}

static esp_err_t pn532_poll_once(void)
{
    static const uint8_t scan_args[] = {0x01, 0x00};
    uint8_t payload[32] = {0};
    size_t payload_len = 0;
    char uid_text[NODE_DRIVER_UID_TEXT_MAX_LEN] = {0};
    esp_err_t err = pn532_run_command(0x4A, scan_args, sizeof(scan_args), payload, sizeof(payload), &payload_len);

    if (err != ESP_OK) {
        if (err == ESP_ERR_TIMEOUT || err == ESP_ERR_INVALID_RESPONSE) {
            pn532_log_poll_failure_once(err);
            if (s_pn532.absent_reported) {
                return ESP_OK;
            }
            s_pn532.absent_reported = true;
            return node_driver_nfc_reader_runtime_submit_scan(s_pn532_config ? s_pn532_config->id : "", NULL);
        }
        return err;
    }
    if (payload_len < 1U) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (payload[0] == 0U) {
        if (s_pn532.absent_reported) {
            return ESP_OK;
        }
        s_pn532.absent_reported = true;
        return node_driver_nfc_reader_runtime_submit_scan(s_pn532_config ? s_pn532_config->id : "", NULL);
    }
    if (payload_len < 6U) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    size_t uid_len = payload[5];
    if (uid_len == 0U || uid_len > NODE_PN532_UID_MAX_LEN || (6U + uid_len) > payload_len) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uid_to_text(&payload[6], uid_len, uid_text, sizeof(uid_text));
    if (!text_present(uid_text)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    s_pn532.poll_fail_debug_logged = false;
    s_pn532.absent_reported = false;
    return node_driver_nfc_reader_runtime_submit_scan(s_pn532_config ? s_pn532_config->id : "", uid_text);
}

static void pn532_task(void *arg)
{
    (void)arg;

    while (true) {
        esp_err_t err = ESP_OK;
        uint32_t now_ms_value = 0;
        uint32_t notifications = 0;

        if (xTaskNotifyWait(0, 0xFFFFFFFFu, &notifications, 0) == pdTRUE) {
            if ((notifications & NODE_PN532_NOTIFY_REINIT) != 0U) {
                ESP_LOGI(TAG, "pn532 manual reinit requested");
                reset_failure_backoff();
                pn532_schedule_transport_recovery(true, true);
            }
        }

        if (!s_pn532.started || !s_pn532_config || !s_pn532_config->enabled) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        if (!s_pn532.session_ready) {
            now_ms_value = now_ms();
            if (s_pn532.next_init_attempt_ms != 0 && now_ms_value < s_pn532.next_init_attempt_ms) {
                uint32_t wait_ms = s_pn532.next_init_attempt_ms - now_ms_value;

                if (wait_ms > 1000U) {
                    wait_ms = 1000U;
                }
                vTaskDelay(pdMS_TO_TICKS(wait_ms));
                continue;
            }
            err = pn532_init_session();
            if (err != ESP_OK) {
                schedule_init_retry(err);
                continue;
            }
        }

        err = pn532_poll_once();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            s_pn532.poll_fail_count++;
            if (should_log_failure(&s_pn532.last_poll_log_ms,
                                   &s_pn532.last_poll_err,
                                   err,
                                   NODE_PN532_LOG_INTERVAL_MS)) {
                ESP_LOGW(TAG,
                         "pn532 poll pending err=%s failures=%lu",
                         esp_err_to_name(err),
                         (unsigned long)s_pn532.poll_fail_count);
            }
            (void)node_driver_nfc_reader_runtime_reset();
            if (s_pn532.poll_fail_count >= NODE_PN532_RECOVERY_HW_RESET_THRESHOLD) {
                pn532_schedule_transport_recovery(true, true);
            } else if (s_pn532.poll_fail_count >= NODE_PN532_RECOVERY_BUS_THRESHOLD) {
                pn532_schedule_transport_recovery(true, false);
            } else {
                pn532_schedule_transport_recovery(false, false);
            }
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        s_pn532.poll_fail_count = 0;
        s_pn532.last_poll_err = ESP_OK;
        s_pn532.last_poll_log_ms = 0;

        uint32_t poll_delay_ms = s_pn532_config->poll_interval_ms;
        if (poll_delay_ms < NODE_PN532_MIN_POLL_INTERVAL_MS) {
            poll_delay_ms = NODE_PN532_MIN_POLL_INTERVAL_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(poll_delay_ms));
    }
}

esp_err_t node_driver_pn532_i2c_start(const node_nfc_reader_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(config->driver_impl, "pn532") != 0 || strcmp(config->bus, "i2c_1") != 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!ensure_pn532_config()) {
        return ESP_ERR_NO_MEM;
    }

    *s_pn532_config = *config;
    s_pn532.started = true;
    s_pn532.bus_ready = false;
    s_pn532.session_ready = false;
    s_pn532.ever_ready = false;
    s_pn532.poll_fail_debug_logged = false;
    s_pn532.absent_reported = false;
    reset_failure_backoff();
    esp_log_level_set("i2c.master", ESP_LOG_NONE);

    if (reset_gpio_configured()) {
        (void)gpio_set_direction((gpio_num_t)s_pn532_config->reset_gpio, GPIO_MODE_OUTPUT);
        (void)gpio_set_level((gpio_num_t)s_pn532_config->reset_gpio, 1);
    }

    if (!s_pn532.task_handle) {
        StackType_t *task_stack = allocate_pn532_task_stack();

        if (!task_stack) {
            s_pn532.started = false;
            return ESP_ERR_NO_MEM;
        }
        s_pn532.task_handle = xTaskCreateStatic(pn532_task,
                                                "node_pn532",
                                                3584,
                                                NULL,
                                                tskIDLE_PRIORITY + 1,
                                                task_stack,
                                                &s_pn532.task_storage);
        if (!s_pn532.task_handle) {
            s_pn532.started = false;
            return ESP_ERR_NO_MEM;
        }
    }
    ESP_LOGI(TAG,
             "pn532 adapter started id=%s addr=0x%02x sda=%d scl=%d rst=%d poll=%lu",
             s_pn532_config ? s_pn532_config->id : "",
             s_pn532_config ? (unsigned)s_pn532_config->i2c_address : 0U,
             s_pn532_config ? s_pn532_config->i2c_sda_gpio : -1,
             s_pn532_config ? s_pn532_config->i2c_scl_gpio : -1,
             s_pn532_config ? s_pn532_config->reset_gpio : -1,
             s_pn532_config ? (unsigned long)s_pn532_config->poll_interval_ms : 0UL);
    return ESP_OK;
}

esp_err_t node_driver_pn532_i2c_request_reinit(void)
{
    if (!s_pn532.started || !s_pn532.task_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskNotify(s_pn532.task_handle, NODE_PN532_NOTIFY_REINIT, eSetBits) != pdPASS) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

void node_driver_pn532_i2c_get_diag(node_pn532_i2c_diag_t *out_diag)
{
    if (!out_diag) {
        return;
    }

    memset(out_diag, 0, sizeof(*out_diag));
    out_diag->started = s_pn532.started;
    out_diag->enabled = s_pn532_config ? s_pn532_config->enabled : false;
    out_diag->bus_ready = s_pn532.bus_ready;
    out_diag->session_ready = s_pn532.session_ready;
    out_diag->ever_ready = s_pn532.ever_ready;
    out_diag->pending_hw_reset = s_pn532.pending_hw_reset;
    out_diag->init_fail_count = s_pn532.init_fail_count;
    out_diag->poll_fail_count = s_pn532.poll_fail_count;
    out_diag->next_init_attempt_ms = s_pn532.next_init_attempt_ms;
    out_diag->last_init_err = s_pn532.last_init_err;
    out_diag->last_poll_err = s_pn532.last_poll_err;
}
