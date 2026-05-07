#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "helix_memory.h"

#define HELIX_POOL_SIZE (96 * 1024)

static EXT_RAM_BSS_ATTR uint8_t s_helix_pool[HELIX_POOL_SIZE];
static size_t s_helix_pool_used = 0;
static uint16_t s_helix_alloc_count = 0;
static portMUX_TYPE s_helix_pool_lock = portMUX_INITIALIZER_UNLOCKED;

static size_t helix_align_size(size_t size)
{
    return (size + 7U) & ~((size_t)7U);
}

void *helix_malloc(int size)
{
    void *ptr = NULL;
    size_t aligned = 0;
    if (size <= 0) {
        return NULL;
    }
    aligned = helix_align_size((size_t)size);
    portENTER_CRITICAL(&s_helix_pool_lock);
    if (aligned <= HELIX_POOL_SIZE - s_helix_pool_used) {
        ptr = &s_helix_pool[s_helix_pool_used];
        s_helix_pool_used += aligned;
        s_helix_alloc_count++;
    }
    portEXIT_CRITICAL(&s_helix_pool_lock);
    if (ptr) {
        memset(ptr, 0, (size_t)size);
    }
    return ptr;
}

void helix_free(void *ptr)
{
    if (!ptr) {
        return;
    }
    portENTER_CRITICAL(&s_helix_pool_lock);
    if (s_helix_alloc_count > 0) {
        s_helix_alloc_count--;
    }
    if (s_helix_alloc_count == 0) {
        s_helix_pool_used = 0;
    }
    portEXIT_CRITICAL(&s_helix_pool_lock);
}
