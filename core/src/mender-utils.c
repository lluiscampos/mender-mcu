/**
 * @file      mender-utils.c
 * @brief     Mender utility functions
 *
 * Copyright joelguittet and mender-mcu-client contributors
 * Copyright Northern.tech AS
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mender-log.h"

/* ASCII unit separator */
#define MENDER_KEY_VALUE_DELIMITER "\x1F"
/* ASCII record separator */
#define MENDER_KEY_VALUE_SEPARATOR "\x1E"

char *
mender_utils_http_status_to_string(int status) {

    /* Definition of status strings */
    const struct {
        uint16_t status;
        char    *str;
    } desc[] = { { 100, "Continue" },
                 { 101, "Switching Protocols" },
                 { 103, "Early Hints" },
                 { 200, "OK" },
                 { 201, "Created" },
                 { 202, "Accepted" },
                 { 203, "Non-Authoritative Information" },
                 { 204, "No Content" },
                 { 205, "Reset Content" },
                 { 206, "Partial Content" },
                 { 300, "Multiple Choices" },
                 { 301, "Moved Permanently" },
                 { 302, "Found" },
                 { 303, "See Other" },
                 { 304, "Not Modified" },
                 { 307, "Temporary Redirect" },
                 { 308, "Permanent Redirect" },
                 { 400, "Bad Request" },
                 { 401, "Unauthorized" },
                 { 402, "Payment Required" },
                 { 403, "Forbidden" },
                 { 404, "Not Found" },
                 { 405, "Method Not Allowed" },
                 { 406, "Not Acceptable" },
                 { 407, "Proxy Authentication Required" },
                 { 408, "Request Timeout" },
                 { 409, "Conflict" },
                 { 410, "Gone" },
                 { 411, "Length Required" },
                 { 412, "Precondition Failed" },
                 { 413, "Payload Too Large" },
                 { 414, "URI Too Long" },
                 { 415, "Unsupported Media Type" },
                 { 416, "Range Not Satisfiable" },
                 { 417, "Expectation Failed" },
                 { 418, "I'm a teapot" },
                 { 422, "Unprocessable Entity" },
                 { 425, "Too Early" },
                 { 426, "Upgrade Required" },
                 { 428, "Precondition Required" },
                 { 429, "Too Many Requests" },
                 { 431, "Request Header Fields Too Large" },
                 { 451, "Unavailable For Legal Reasons" },
                 { 500, "Internal Server Error" },
                 { 501, "Not Implemented" },
                 { 502, "Bad Gateway" },
                 { 503, "Service Unavailable" },
                 { 504, "Gateway Timeout" },
                 { 505, "HTTP Version Not Supported" },
                 { 506, "Variant Also Negotiates" },
                 { 507, "Insufficient Storage" },
                 { 508, "Loop Detected" },
                 { 510, "Not Extended" },
                 { 511, "Network Authentication Required" } };

    /* Return HTTP status as string */
    for (int index = 0; index < sizeof(desc) / sizeof(desc[0]); index++) {
        if (desc[index].status == status) {
            return desc[index].str;
        }
    }

    return NULL;
}

char *
mender_utils_strrstr(const char *haystack, const char *needle) {

    assert(NULL != haystack);
    assert(NULL != needle);

    char *r = NULL;

    if (!needle[0]) {
        return (char *)haystack + strlen(haystack);
    }

    while (1) {
        char *p = strstr(haystack, needle);
        if (!p) {
            return r;
        }
        r        = p;
        haystack = p + 1;
    }
}

bool
mender_utils_strbeginwith(const char *s1, const char *s2) {

    /* Check parameters */
    if ((NULL == s1) || (NULL == s2)) {
        return false;
    }

    /* Compare the beginning of the string */
    return (0 == strncmp(s1, s2, strlen(s2)));
}

bool
mender_utils_strendwith(const char *s1, const char *s2) {

    /* Check parameters */
    if ((NULL == s1) || (NULL == s2)) {
        return false;
    }

    /* Compare the end of the string */
    return (0 == strncmp(s1 + strlen(s1) - strlen(s2), s2, strlen(s2)));
}

char *
mender_utils_deployment_status_to_string(mender_deployment_status_t deployment_status) {

    /* Return deployment status as string */
    if (MENDER_DEPLOYMENT_STATUS_DOWNLOADING == deployment_status) {
        return "downloading";
    } else if (MENDER_DEPLOYMENT_STATUS_INSTALLING == deployment_status) {
        return "installing";
    } else if (MENDER_DEPLOYMENT_STATUS_REBOOTING == deployment_status) {
        return "rebooting";
    } else if (MENDER_DEPLOYMENT_STATUS_SUCCESS == deployment_status) {
        return "success";
    } else if (MENDER_DEPLOYMENT_STATUS_FAILURE == deployment_status) {
        return "failure";
    } else if (MENDER_DEPLOYMENT_STATUS_ALREADY_INSTALLED == deployment_status) {
        return "already-installed";
    }

    return NULL;
}

mender_keystore_t *
mender_utils_keystore_new(size_t length) {

    /* Allocate memory */
    mender_keystore_t *keystore = (mender_keystore_t *)malloc((length + 1) * sizeof(mender_item_t));
    if (NULL == keystore) {
        mender_log_error("Unable to allocate memory");
        return NULL;
    }

    /* Initialize keystore */
    memset(keystore, 0, (length + 1) * sizeof(mender_item_t));

    return keystore;
}

mender_err_t
mender_utils_keystore_copy(mender_keystore_t **dst_keystore, mender_keystore_t *src_keystore) {

    assert(NULL != dst_keystore);
    mender_err_t ret = MENDER_OK;

    /* Copy the new keystore */
    size_t length = mender_utils_keystore_length(src_keystore);
    if (NULL == (*dst_keystore = mender_utils_keystore_new(length))) {
        mender_log_error("Unable to allocate memory");
        ret = MENDER_FAIL;
        goto END;
    }
    for (size_t index = 0; index < length; index++) {
        if (MENDER_OK != (ret = mender_utils_keystore_set_item(*dst_keystore, index, src_keystore[index].name, src_keystore[index].value))) {
            mender_log_error("Unable to allocate memory");
            goto END;
        }
    }

END:

    return ret;
}

mender_err_t
mender_utils_keystore_from_json(mender_keystore_t **keystore, cJSON *object) {

    assert(NULL != keystore);
    mender_err_t ret;

    /* Release previous keystore */
    if (MENDER_OK != (ret = mender_utils_keystore_delete(*keystore))) {
        mender_log_error("Unable to delete keystore");
        return ret;
    }
    *keystore = NULL;

    /* Set key-store */
    if (NULL != object) {
        size_t length       = 0;
        cJSON *current_item = object->child;
        while (NULL != current_item) {
            if ((NULL != current_item->string) && (NULL != current_item->valuestring)) {
                length++;
            }
            current_item = current_item->next;
        }
        if (NULL != (*keystore = mender_utils_keystore_new(length))) {
            size_t index = 0;
            current_item = object->child;
            while (NULL != current_item) {
                if ((NULL != current_item->string) && (NULL != current_item->valuestring)) {
                    if (MENDER_OK != (ret = mender_utils_keystore_set_item(*keystore, index, current_item->string, current_item->valuestring))) {
                        mender_log_error("Unable to allocate memory");
                        return ret;
                    }
                    index++;
                }
                current_item = current_item->next;
            }
        } else {
            mender_log_error("Unable to allocate memory");
            ret = MENDER_FAIL;
        }
    }

    return ret;
}

mender_err_t
mender_utils_keystore_to_json(mender_keystore_t *keystore, cJSON **object) {

    assert(NULL != object);

    /* Format data */
    *object = cJSON_CreateObject();
    if (NULL == *object) {
        mender_log_error("Unable to allocate memory");
        return MENDER_FAIL;
    }
    if (NULL != keystore) {
        size_t index = 0;
        while ((NULL != keystore[index].name) && (NULL != keystore[index].value)) {
            cJSON_AddStringToObject(*object, keystore[index].name, keystore[index].value);
            index++;
        }
    }

    return MENDER_OK;
}

mender_err_t
mender_utils_keystore_set_item(mender_keystore_t *keystore, size_t index, char *name, char *value) {

    assert(NULL != keystore);

    /* Release memory */
    if (NULL != keystore[index].name) {
        free(keystore[index].name);
        keystore[index].name = NULL;
    }
    if (NULL != keystore[index].value) {
        free(keystore[index].value);
        keystore[index].value = NULL;
    }

    /* Copy name and value */
    if (NULL != name) {
        if (NULL == (keystore[index].name = strdup(name))) {
            mender_log_error("Unable to allocate memory");
            return MENDER_FAIL;
        }
    }
    if (NULL != value) {
        if (NULL == (keystore[index].value = strdup(value))) {
            mender_log_error("Unable to allocate memory");
            return MENDER_FAIL;
        }
    }

    return MENDER_OK;
}

size_t
mender_utils_keystore_length(mender_keystore_t *keystore) {

    /* Compute key-store length */
    size_t length = 0;
    if (NULL != keystore) {
        while ((NULL != keystore[length].name) && (NULL != keystore[length].value)) {
            length++;
        }
    }

    return length;
}

mender_err_t
mender_utils_keystore_delete(mender_keystore_t *keystore) {

    /* Release memory */
    if (NULL != keystore) {
        size_t index = 0;
        while ((NULL != keystore[index].name) || (NULL != keystore[index].value)) {
            if (NULL != keystore[index].name) {
                free(keystore[index].name);
            }
            if (NULL != keystore[index].value) {
                free(keystore[index].value);
            }
            index++;
        }
        free(keystore);
    }

    return MENDER_OK;
}

mender_err_t
mender_utils_identity_to_json(mender_identity_t *identity, cJSON **object) {

    assert(NULL != object);

    /* Format data */
    *object = cJSON_CreateObject();
    if (NULL == *object) {
        mender_log_error("Unable to allocate memory");
        return MENDER_FAIL;
    }
    if (NULL == cJSON_AddStringToObject(*object, identity->name, identity->value)) {
        mender_log_error("Unable to add identity to JSON object");
        return MENDER_FAIL;
    }
    return MENDER_OK;
}

mender_err_t
mender_utils_free_linked_list(mender_key_value_list_t *list) {
    mender_key_value_list_t *item = list;
    while (NULL != item) {
        mender_key_value_list_t *next = item->next;
        free(item->key);
        free(item->value);
        free(item);
        item = next;
    }
    return MENDER_OK;
}
mender_err_t
mender_utils_create_key_value_node(const char *type, const char *value, mender_key_value_list_t **list) {

    assert(NULL != type);
    assert(NULL != value);
    assert(NULL != list);

    mender_key_value_list_t *item = (mender_key_value_list_t *)calloc(1, sizeof(mender_key_value_list_t));
    if (NULL == item) {
        mender_log_error("Unable to allocate memory for linked list node");
        return MENDER_FAIL;
    }

    item->key = strdup(type);
    if (NULL == item->key) {
        mender_log_error("Unable to allocate memory for type");
        goto ERROR;
    }

    item->value = strdup(value);
    if (NULL == item->value) {
        mender_log_error("Unable to allocate memory for value");
        goto ERROR;
    }

    item->next = *list;
    *list      = item;

    return MENDER_OK;

ERROR:
    mender_utils_free_linked_list(item);
    return MENDER_FAIL;
}

mender_err_t
mender_utils_key_value_list_to_string(mender_key_value_list_t *list, char **key_value_str) {

    /*
     * Converts key-value linked list to string of format :
     *      "key<\x1F>value<\x1E>...key<\x1Fvalue<\x1E>"
     *  Where \x1F is the ASCII unit separator and \x1E is the ASCII record separator
     * */

    /* Start with 1 for the null terminator */
    size_t total_len = 1;
    for (mender_key_value_list_t *item = list; NULL != item; item = item->next) {
        if (NULL != item->key && NULL != item->value) {
            total_len += strlen(item->key) + strlen(item->value) + 3; // key=value<space>
        }
    }

    *key_value_str = (char *)calloc(1, total_len);
    if (NULL == *key_value_str) {
        mender_log_error("Unable to allocate memory for string");
        return MENDER_FAIL;
    }

    /* Pointer to key_value_str pointer */
    char *str_ptr = *key_value_str;
    for (mender_key_value_list_t *item = list; NULL != item; item = item->next) {
        if (NULL != item->key && NULL != item->value) {
            int ret = snprintf(
                str_ptr, total_len - (str_ptr - *key_value_str), "%s" MENDER_KEY_VALUE_DELIMITER "%s" MENDER_KEY_VALUE_SEPARATOR, item->key, item->value);
            if (0 > ret) {
                mender_log_error("Unable to write to string");
                return MENDER_FAIL;
            }
            str_ptr += ret;
        }
    }

    return MENDER_OK;
}

mender_err_t
mender_utils_string_to_key_value_list(const char *key_value_str, mender_key_value_list_t **list) {

    /*
     * Converts of format:
     *      "key<\x1F>value<\x1E>...key<\x1Fvalue<\x1E>"
     *  to key-value linked list
     *  Where \x1F is the ASCII unit separator and \x1E is the ASCII record separator
     * */

    assert(NULL != key_value_str);
    assert(NULL != list);

    char *str = strdup(key_value_str);
    if (NULL == str) {
        mender_log_error("Unable to allocate memory for string");
        return MENDER_FAIL;
    }
    char *saveptr;
    char *token = strtok_r(str, MENDER_KEY_VALUE_SEPARATOR, &saveptr);

    mender_err_t ret = MENDER_FAIL;

    char *delimiter_pos = NULL;
    while (NULL != token) {
        delimiter_pos = strchr(token, MENDER_KEY_VALUE_DELIMITER[0]);
        if (NULL == delimiter_pos) {
            mender_log_error("Invalid key-value string");
            goto END;
        }
        /* Add null terminator to split key and value to get the key from the token */
        token[delimiter_pos - token] = '\0';
        if (MENDER_OK != mender_utils_create_key_value_node(token, delimiter_pos + 1, list)) {
            mender_log_error("Unable to create key-value node");
            goto END;
        }
        token = strtok_r(NULL, MENDER_KEY_VALUE_SEPARATOR, &saveptr);
    }

    ret = MENDER_OK;
END:
    free(str);
    return ret;
}

mender_err_t
mender_utils_append_list(mender_key_value_list_t **list1, mender_key_value_list_t **list2) {

    /* Combine two linked lists by pointing the last element of the first list
     * to the first element of the second list
     * Sets list2 to NULL
     * */

    mender_key_value_list_t *item = *list1;
    if (NULL != item) {
        while (NULL != item->next) {
            item = item->next;
        }
        item->next = *list2;
    } else {
        *list1 = *list2;
    }
    *list2 = NULL;
    return MENDER_OK;
}
