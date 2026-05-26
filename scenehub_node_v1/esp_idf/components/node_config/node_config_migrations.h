#pragma once

#include "node_config_legacy.h"

void node_config_migrate_v1(const node_config_v1_t *legacy, node_config_t *config);
void node_config_migrate_v2(const node_config_v2_t *legacy, node_config_t *config);
void node_config_migrate_v3(const node_config_v3_t *legacy, node_config_t *config);
void node_config_migrate_v4(const node_config_v4_t *legacy, node_config_t *config);
void node_config_migrate_v5(const node_config_v5_t *legacy, node_config_t *config);
void node_config_migrate_v6(const node_config_v6_t *legacy, node_config_t *config);
void node_config_migrate_v7(const node_config_v7_t *legacy, node_config_t *config);
void node_config_migrate_v8(const node_config_v8_t *legacy, node_config_t *config);
