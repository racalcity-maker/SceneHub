#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "config_store.h"

static bool hash_is_zero(const uint8_t hash[CONFIG_STORE_AUTH_HASH_LEN])
{
    for (size_t i = 0; i < CONFIG_STORE_AUTH_HASH_LEN; ++i) {
        if (hash[i] != 0) {
            return false;
        }
    }
    return true;
}

static void test_config_store_hash_password_is_deterministic_and_nonzero(void)
{
    uint8_t first[CONFIG_STORE_AUTH_HASH_LEN] = {0};
    uint8_t second[CONFIG_STORE_AUTH_HASH_LEN] = {0};
    uint8_t different[CONFIG_STORE_AUTH_HASH_LEN] = {0};

    config_store_hash_password("admin", first);
    config_store_hash_password("admin", second);
    config_store_hash_password("other", different);

    TEST_ASSERT_FALSE(hash_is_zero(first));
    TEST_ASSERT_EQUAL_MEMORY(first, second, sizeof(first));
    TEST_ASSERT_NOT_EQUAL(0, memcmp(first, different, sizeof(first)));
}

static void test_config_store_hash_password_treats_null_as_empty(void)
{
    uint8_t null_hash[CONFIG_STORE_AUTH_HASH_LEN] = {0};
    uint8_t empty_hash[CONFIG_STORE_AUTH_HASH_LEN] = {0};

    config_store_hash_password(NULL, null_hash);
    config_store_hash_password("", empty_hash);

    TEST_ASSERT_FALSE(hash_is_zero(null_hash));
    TEST_ASSERT_EQUAL_MEMORY(empty_hash, null_hash, sizeof(empty_hash));
}

static void test_config_store_hash_password_ignores_null_output(void)
{
    config_store_hash_password("admin", NULL);
}

static void test_config_store_has_web_user_requires_username_and_hash(void)
{
    app_config_t cfg = {0};
    uint8_t hash[CONFIG_STORE_AUTH_HASH_LEN] = {0};

    TEST_ASSERT_FALSE(config_store_has_web_user(NULL));
    TEST_ASSERT_FALSE(config_store_has_web_user(&cfg));

    config_store_hash_password("user-pass", hash);
    memcpy(cfg.web_user.password_hash, hash, sizeof(hash));
    TEST_ASSERT_FALSE(config_store_has_web_user(&cfg));

    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.web_user.username, sizeof(cfg.web_user.username), "%s", "operator");
    TEST_ASSERT_FALSE(config_store_has_web_user(&cfg));

    memcpy(cfg.web_user.password_hash, hash, sizeof(hash));
    TEST_ASSERT_TRUE(config_store_has_web_user(&cfg));
}

void register_config_store_utils_tests(void)
{
    RUN_TEST(test_config_store_hash_password_is_deterministic_and_nonzero);
    RUN_TEST(test_config_store_hash_password_treats_null_as_empty);
    RUN_TEST(test_config_store_hash_password_ignores_null_output);
    RUN_TEST(test_config_store_has_web_user_requires_username_and_hash);
}
