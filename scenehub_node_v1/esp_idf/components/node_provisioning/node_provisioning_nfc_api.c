#include "node_provisioning_config_api_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static bool text_has_nonspace(const char *text)
{
    if (!text) {
        return false;
    }
    while (*text != '\0') {
        if (!isspace((unsigned char)*text)) {
            return true;
        }
        ++text;
    }
    return false;
}

static bool json_string_optional_copy(const cJSON *item, char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return false;
    }
    out[0] = '\0';
    if (!item || cJSON_IsNull(item)) {
        return true;
    }
    if (!cJSON_IsString(item) || !item->valuestring) {
        return false;
    }
    if (strlen(item->valuestring) >= out_size) {
        return false;
    }
    snprintf(out, out_size, "%s", item->valuestring);
    return true;
}

static esp_err_t parse_nfc_cards_request(const char *json,
                                         node_nfc_known_card_t *cards,
                                         size_t card_capacity,
                                         size_t *out_count)
{
    cJSON *root = NULL;
    cJSON *known_cards = NULL;
    size_t count = 0;

    if (!json || !cards || !out_count || card_capacity == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(cards, 0, sizeof(cards[0]) * card_capacity);
    *out_count = 0;

    root = cJSON_Parse(json);
    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }

    known_cards = cJSON_GetObjectItemCaseSensitive(root, "known_cards");
    if (!cJSON_IsArray(known_cards)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *card = NULL;
    cJSON_ArrayForEach(card, known_cards) {
        cJSON *name = NULL;
        cJSON *uid = NULL;
        cJSON *token_id = NULL;
        cJSON *event = NULL;

        if (!cJSON_IsObject(card) || count >= card_capacity) {
            cJSON_Delete(root);
            return ESP_ERR_INVALID_ARG;
        }

        name = cJSON_GetObjectItemCaseSensitive(card, "name");
        uid = cJSON_GetObjectItemCaseSensitive(card, "uid");
        token_id = cJSON_GetObjectItemCaseSensitive(card, "token_id");
        event = cJSON_GetObjectItemCaseSensitive(card, "event");

        if (!json_string_optional_copy(name, cards[count].name, sizeof(cards[count].name)) ||
            !cJSON_IsString(uid) || !uid->valuestring ||
            strlen(uid->valuestring) >= sizeof(cards[count].uid) ||
            !text_has_nonspace(uid->valuestring) ||
            !json_string_optional_copy(event, cards[count].event_name, sizeof(cards[count].event_name))) {
            cJSON_Delete(root);
            return ESP_ERR_INVALID_ARG;
        }

        snprintf(cards[count].uid, sizeof(cards[count].uid), "%s", uid->valuestring);
        cards[count].token_id = 0;
        if (token_id) {
            if (!cJSON_IsNumber(token_id)) {
                cJSON_Delete(root);
                return ESP_ERR_INVALID_ARG;
            }
            cards[count].token_id = (int32_t)token_id->valuedouble;
        }
        ++count;
    }

    *out_count = count;
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t node_provisioning_nfc_reader_post(httpd_req_t *req)
{
    enum { MAX_BODY = 8192 };
    char action[24] = {0};
    size_t known_card_count = 0;
    node_admin_control_result_t admin_result = {0};
    esp_err_t err = ESP_OK;

    if (req->content_len <= 0 || req->content_len > MAX_BODY) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body size");
    }
    if (!ensure_provisioning_scratch()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config no mem");
    }
    if (!lock_post_body()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config busy");
    }
    if (!ensure_post_body_buffer()) {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config no mem");
    }
    if (!read_request_body(req, s_post_body, NODE_PROVISIONING_POST_BODY_CAPACITY)) {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "body read failed");
    }
    if (json_copy_string_field(s_post_body, "action", action, sizeof(action))) {
        if (strcmp(action, "reinit") == 0) {
            err = node_admin_control_reinit_nfc(&admin_result);
            unlock_post_body();
            if (err != ESP_OK || !admin_result.applied) {
                ESP_LOGW(g_node_provisioning_tag, "nfc reader reinit failed: %s", esp_err_to_name(err));
                return send_admin_result_json(req,
                                              HTTPD_500_INTERNAL_SERVER_ERROR,
                                              false,
                                              "reinit_failed",
                                              &admin_result);
            }
            return send_admin_result_json(req, 200, true, "", &admin_result);
        }
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid action");
    }
    err = parse_nfc_cards_request(s_post_body,
                                  s_nfc_cards_scratch,
                                  NODE_DRIVER_NFC_KNOWN_CARD_MAX,
                                  &known_card_count);
    if (err != ESP_OK) {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid known_cards");
    }

    err = node_admin_control_save_nfc_cards(s_nfc_cards_scratch, known_card_count, &admin_result);
    unlock_post_body();
    if (err != ESP_OK || !admin_result.applied) {
        ESP_LOGE(g_node_provisioning_tag, "nfc cards save failed: %s", esp_err_to_name(err));
        return send_admin_result_json(req,
                                      HTTPD_500_INTERNAL_SERVER_ERROR,
                                      false,
                                      "save_failed",
                                      &admin_result);
    }
    return send_admin_result_json(req, 200, true, "", &admin_result);
}
