#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "quest_common_limits.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ORCH_REGISTRY_STATE_MAX_LEN
#define ORCH_REGISTRY_STATE_MAX_LEN 16
#endif
#ifndef ORCH_REGISTRY_ISSUE_ID_MAX_LEN
#define ORCH_REGISTRY_ISSUE_ID_MAX_LEN 96
#endif
#ifndef ORCH_REGISTRY_ISSUE_CODE_MAX_LEN
#define ORCH_REGISTRY_ISSUE_CODE_MAX_LEN 32
#endif
#ifndef ORCH_REGISTRY_ISSUE_TITLE_MAX_LEN
#define ORCH_REGISTRY_ISSUE_TITLE_MAX_LEN 64
#endif
#ifndef ORCH_REGISTRY_ISSUE_DETAILS_MAX_LEN
#define ORCH_REGISTRY_ISSUE_DETAILS_MAX_LEN 160
#endif

typedef enum {
    ORCH_ISSUE_SCOPE_SYSTEM = 0,
    ORCH_ISSUE_SCOPE_ROOM,
    ORCH_ISSUE_SCOPE_DEVICE,
} orch_issue_scope_t;

typedef enum {
    ORCH_ISSUE_SEVERITY_INFO = 0,
    ORCH_ISSUE_SEVERITY_WARNING,
    ORCH_ISSUE_SEVERITY_ERROR,
} orch_issue_severity_t;

typedef struct orch_issue_entry_t {
    char issue_id[ORCH_REGISTRY_ISSUE_ID_MAX_LEN];
    char room_id[QUEST_ROOM_ID_MAX_LEN];
    char device_id[QUEST_ID_MAX_LEN];
    char code[ORCH_REGISTRY_ISSUE_CODE_MAX_LEN];
    char title[ORCH_REGISTRY_ISSUE_TITLE_MAX_LEN];
    char details[ORCH_REGISTRY_ISSUE_DETAILS_MAX_LEN];
    orch_issue_scope_t scope;
    char scope_text[ORCH_REGISTRY_STATE_MAX_LEN];
    orch_issue_severity_t severity;
    char severity_text[ORCH_REGISTRY_STATE_MAX_LEN];
    bool active;
} orch_issue_entry_t;

#ifdef __cplusplus
}
#endif
