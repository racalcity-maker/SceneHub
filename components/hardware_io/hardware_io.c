#include "hardware_io.h"

#include "hardware_io_internal.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_lock = NULL;
static StaticSemaphore_t s_lock_storage;
static portMUX_TYPE s_init_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_initialized = false;

static esp_err_t hardware_io_ensure_lock(void)
{
    if (s_lock) {
        return ESP_OK;
    }
    portENTER_CRITICAL(&s_init_lock);
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    }
    portEXIT_CRITICAL(&s_init_lock);
    return s_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t hardware_io_lock(void)
{
    esp_err_t err = hardware_io_ensure_lock();
    if (err != ESP_OK) {
        return err;
    }
    return xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

void hardware_io_unlock(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

uint64_t hardware_io_now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

esp_err_t hardware_io_init(void)
{
    esp_err_t err = hardware_io_ensure_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = hardware_io_lock();
    if (err != ESP_OK) {
        return err;
    }
    if (s_initialized) {
        hardware_io_unlock();
        return ESP_OK;
    }

    err = hardware_io_relay_init_locked();
    if (err == ESP_OK) {
        err = hardware_io_mosfet_init_locked();
    }
    if (err == ESP_OK) {
        err = hardware_io_input_init_locked();
    }
    if (err == ESP_OK) {
        err = hardware_io_gpio_init_locked();
    }
    if (err != ESP_OK) {
        hardware_io_unlock();
        return err;
    }

    s_initialized = true;
    hardware_io_unlock();
    return ESP_OK;
}

bool hardware_io_is_available(void)
{
    return s_initialized;
}

esp_err_t hardware_io_safe_off_all(void)
{
    esp_err_t err = ESP_OK;
    esp_err_t first_err = ESP_OK;
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    err = hardware_io_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = hardware_io_relay_safe_off_all_locked();
    if (err != ESP_OK && first_err == ESP_OK) {
        first_err = err;
    }
    err = hardware_io_mosfet_safe_off_all_locked();
    if (err != ESP_OK && first_err == ESP_OK) {
        first_err = err;
    }
    err = hardware_io_gpio_safe_off_all_locked();
    if (err != ESP_OK && first_err == ESP_OK) {
        first_err = err;
    }
    hardware_io_unlock();
    return first_err;
}
