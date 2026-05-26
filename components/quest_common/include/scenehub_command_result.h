#pragma once

#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCENEHUB_COMMAND_RESULT_ACCEPTED "accepted"
#define SCENEHUB_COMMAND_RESULT_STARTED "started"
#define SCENEHUB_COMMAND_RESULT_DONE "done"
#define SCENEHUB_COMMAND_RESULT_FAILED "failed"
#define SCENEHUB_COMMAND_RESULT_REJECTED "rejected"
#define SCENEHUB_COMMAND_RESULT_TIMEOUT "timeout"

static inline bool scenehub_command_result_streq(const char *lhs, const char *rhs)
{
    return lhs && rhs && strcmp(lhs, rhs) == 0;
}

static inline const char *scenehub_command_result_normalize(const char *status)
{
    if (!status || !status[0]) {
        return "";
    }
    if (scenehub_command_result_streq(status, "ok")) {
        return SCENEHUB_COMMAND_RESULT_DONE;
    }
    if (scenehub_command_result_streq(status, "error")) {
        return SCENEHUB_COMMAND_RESULT_FAILED;
    }
    return status;
}

static inline bool scenehub_command_result_is_pending(const char *status)
{
    return scenehub_command_result_streq(status, SCENEHUB_COMMAND_RESULT_ACCEPTED) ||
           scenehub_command_result_streq(status, SCENEHUB_COMMAND_RESULT_STARTED);
}

static inline bool scenehub_command_result_is_success(const char *status)
{
    return scenehub_command_result_streq(status, SCENEHUB_COMMAND_RESULT_DONE);
}

static inline bool scenehub_command_result_is_failure(const char *status)
{
    return scenehub_command_result_streq(status, SCENEHUB_COMMAND_RESULT_FAILED) ||
           scenehub_command_result_streq(status, SCENEHUB_COMMAND_RESULT_REJECTED) ||
           scenehub_command_result_streq(status, SCENEHUB_COMMAND_RESULT_TIMEOUT);
}

static inline bool scenehub_command_result_is_terminal(const char *status)
{
    return scenehub_command_result_is_success(status) ||
           scenehub_command_result_is_failure(status);
}

#ifdef __cplusplus
}
#endif
