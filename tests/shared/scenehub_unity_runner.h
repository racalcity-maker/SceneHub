#pragma once

#include <stddef.h>

typedef void (*scenehub_test_register_fn_t)(void);

typedef struct {
    const scenehub_test_register_fn_t *registrars;
    size_t registrar_count;
} scenehub_test_suite_t;

void scenehub_test_runner_start(const scenehub_test_suite_t *suite);
