/**
 * @file      mender-api.c
 * @brief     Implementation of the Mender API
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

#define _GNU_SOURCE // asprintf
#include <stdio.h>  // asprintf

#include "mender-api.h"
#include "mender-artifact.h"
#include "mender-storage.h"
#include "mender-http.h"
#include "mender-log.h"
#include "mender-tls.h"

/**
 * @brief Paths of the mender-server APIs
 */
#define MENDER_API_PATH_POST_AUTHENTICATION_REQUESTS "/api/devices/v1/authentication/auth_requests"
#define MENDER_API_PATH_GET_NEXT_DEPLOYMENT          "/api/devices/v1/deployments/device/deployments/next"
#define MENDER_API_PATH_POST_NEXT_DEPLOYMENT_V2      "/api/devices/v2/deployments/device/deployments/next"
#define MENDER_API_PATH_PUT_DEPLOYMENT_STATUS        "/api/devices/v1/deployments/device/deployments/%s/status"
#define MENDER_API_PATH_GET_DEVICE_CONFIGURATION     "/api/devices/v1/deviceconfig/configuration"
#define MENDER_API_PATH_PUT_DEVICE_CONFIGURATION     "/api/devices/v1/deviceconfig/configuration"
#define MENDER_API_PATH_GET_DEVICE_CONNECT           "/api/devices/v1/deviceconnect/connect"
#define MENDER_API_PATH_PUT_DEVICE_ATTRIBUTES        "/api/devices/v1/inventory/device/attributes"

/**
 * @brief Mender API configuration
 */
static mender_api_config_t mender_api_config;

/**
 * @brief Authentication token
 */
static char *mender_api_jwt = NULL;

/**
 * @brief HTTP callback used to handle text content
 * @param event HTTP client event
 * @param data Data received
 * @param data_length Data length
 * @param params Callback parameters
 * @return MENDER_OK if the function succeeds, error code otherwise
 */
static mender_err_t mender_api_http_text_callback(mender_http_client_event_t event, void *data, size_t data_length, void *params);

/**
 * @brief HTTP callback used to handle artifact content
 * @param event HTTP client event
 * @param data Data received
 * @param data_length Data length
 * @param params Callback parameters
 * @return MENDER_OK if the function succeeds, error code otherwise
 */
static mender_err_t mender_api_http_artifact_callback(mender_http_client_event_t event, void *data, size_t data_length, void *params);

/**
 * @brief Artifact name variable
 */
static char *artifact_name = NULL;

/**
 * @brief Print response error
 * @param response HTTP response, NULL if not available
 * @param status HTTP status
 */
static void mender_api_print_response_error(char *response, int status);

mender_err_t
mender_api_init(mender_api_config_t *config) {

    assert(NULL != config);
    assert(NULL != config->device_type);
    assert(NULL != config->host);
    mender_err_t ret;

    /* Load and set artifact_name here */
    if ((MENDER_OK != mender_storage_get_artifact_name(&artifact_name)) && (NULL != artifact_name)) {
        mender_log_error("Unable to get artifact name");
        return MENDER_FAIL;
    }
    /* Save configuration */
    memcpy(&mender_api_config, config, sizeof(mender_api_config_t));

    /* Initializations */
    mender_http_config_t mender_http_config = { .host = mender_api_config.host };
    if (MENDER_OK != (ret = mender_http_init(&mender_http_config))) {
        mender_log_error("Unable to initialize HTTP");
        return ret;
    }

    return ret;
}

bool
mender_api_is_authenticated(void) {
    return NULL != mender_api_jwt;
}

mender_err_t
mender_api_perform_authentication(mender_err_t (*get_identity)(mender_identity_t **identity)) {

    assert(NULL != get_identity);
    mender_err_t       ret;
    char              *public_key_pem       = NULL;
    cJSON             *json_identity        = NULL;
    mender_identity_t *identity             = NULL;
    char              *unformatted_identity = NULL;
    cJSON             *json_payload         = NULL;
    char              *payload              = NULL;
    char              *response             = NULL;
    char              *signature            = NULL;
    size_t             signature_length     = 0;
    int                status               = 0;

    /* Get public key in PEM format */
    if (MENDER_OK != (ret = mender_tls_get_public_key_pem(&public_key_pem))) {
        mender_log_error("Unable to get public key");
        goto END;
    }

    /* Get identity */
    if (MENDER_OK != (ret = get_identity(&identity))) {
        mender_log_error("Unable to get identity");
        goto END;
    }

    /* Format identity */
    if (MENDER_OK != (ret = mender_utils_identity_to_json(identity, &json_identity))) {
        mender_log_error("Unable to format identity");
        goto END;
    }
    if (NULL == (unformatted_identity = cJSON_PrintUnformatted(json_identity))) {
        mender_log_error("Unable to allocate memory");
        ret = MENDER_FAIL;
        goto END;
    }

    /* Format payload */
    if (NULL == (json_payload = cJSON_CreateObject())) {
        mender_log_error("Unable to allocate memory");
        ret = MENDER_FAIL;
        goto END;
    }
    cJSON_AddStringToObject(json_payload, "id_data", unformatted_identity);
    cJSON_AddStringToObject(json_payload, "pubkey", public_key_pem);
    if (NULL != mender_api_config.tenant_token) {
        cJSON_AddStringToObject(json_payload, "tenant_token", mender_api_config.tenant_token);
    }
    if (NULL == (payload = cJSON_PrintUnformatted(json_payload))) {
        mender_log_error("Unable to allocate memory");
        ret = MENDER_FAIL;
        goto END;
    }

    /* Sign payload */
    if (MENDER_OK != (ret = mender_tls_sign_payload(payload, &signature, &signature_length))) {
        mender_log_error("Unable to sign payload");
        goto END;
    }

    /* Perform HTTP request */
    if (MENDER_OK
        != (ret = mender_http_perform(NULL,
                                      MENDER_API_PATH_POST_AUTHENTICATION_REQUESTS,
                                      MENDER_HTTP_POST,
                                      payload,
                                      signature,
                                      &mender_api_http_text_callback,
                                      (void *)&response,
                                      &status))) {
        mender_log_error("Unable to perform HTTP request");
        goto END;
    }

    /* Treatment depending of the status */
    if (200 == status) {
        if (NULL == response) {
            mender_log_error("Response is empty");
            ret = MENDER_FAIL;
            goto END;
        }
        if (NULL != mender_api_jwt) {
            free(mender_api_jwt);
        }
        if (NULL == (mender_api_jwt = strdup(response))) {
            mender_log_error("Unable to allocate memory");
            ret = MENDER_FAIL;
            goto END;
        }
        ret = MENDER_OK;
    } else {
        mender_api_print_response_error(response, status);
        ret = MENDER_FAIL;
    }

END:

    /* Release memory */
    free(unformatted_identity);
    free(response);
    free(signature);
    free(payload);
    cJSON_Delete(json_payload);
    cJSON_Delete(json_identity);
    free(public_key_pem);

    return ret;
}

static mender_err_t
api_check_for_deployment_v2(int *status, void *response) {
    assert(NULL != status);
    assert(NULL != response);
    assert(NULL != artifact_name);

    mender_err_t ret          = MENDER_FAIL;
    cJSON       *json_payload = NULL;
    char        *payload      = NULL;

    /* Create payload */
    if (NULL == (json_payload = cJSON_CreateObject())) {
        mender_log_error("Unable to allocate memory");
        goto END;
    }

    /* Add "device_provides" entity to payload */
    cJSON *json_provides = NULL;
    if (NULL == (json_provides = cJSON_AddObjectToObject(json_payload, "device_provides"))) {
        mender_log_error("Unable to allocate memory");
        goto END;
    }

    if (NULL == cJSON_AddStringToObject(json_provides, "device_type", mender_api_config.device_type)) {
        mender_log_error("Unable to allocate memory");
        goto END;
    }

#ifdef CONFIG_MENDER_PROVIDES_DEPENDS
#ifdef CONFIG_MENDER_FULL_PARSE_ARTIFACT
    /* Add provides from storage */
    mender_key_value_list_t *provides = NULL;
    if (MENDER_FAIL == mender_storage_get_provides(&provides)) {
        mender_log_error("Unable to get provides");
        goto END;
    }
    for (mender_key_value_list_t *item = provides; NULL != item; item = item->next) {
        if (NULL == cJSON_AddStringToObject(json_provides, item->key, item->value)) {
            mender_log_error("Unable to allocate memory");
            goto END;
        }
    }
#endif /* CONFIG_MENDER_FULL_PARSE_ARTIFACT */
#endif /* CONFIG_MENDER_PROVIDES_DEPENDS */

    if (NULL == cJSON_AddStringToObject(json_provides, "artifact_name", artifact_name)) {
        mender_log_error("Unable to allocate memory");
        goto END;
    }

    if (NULL == (payload = cJSON_PrintUnformatted(json_payload))) {
        mender_log_error("Unable to allocate memory");
        goto END;
    }

    /* Perform HTTP request */
    if (MENDER_OK
        != (ret = mender_http_perform(mender_api_jwt,
                                      MENDER_API_PATH_POST_NEXT_DEPLOYMENT_V2,
                                      MENDER_HTTP_POST,
                                      payload,
                                      NULL,
                                      &mender_api_http_text_callback,
                                      (void *)response,
                                      status))) {
        mender_log_error("Unable to perform HTTP request");
        goto END;
    }

    ret = MENDER_OK;

END:

#ifdef CONFIG_MENDER_PROVIDES_DEPENDS
#ifdef CONFIG_MENDER_FULL_PARSE_ARTIFACT
    mender_utils_free_linked_list(provides);
#endif /* CONFIG_MENDER_FULL_PARSE_ARTIFACT */
#endif /* CONFIG_MENDER_PROVIDES_DEPENDS */
    cJSON_Delete(json_payload);
    free(payload);
    return ret;
}

static mender_err_t
api_check_for_deployment_v1(int *status, void *response) {

    assert(NULL != status);
    assert(NULL != response);
    assert(NULL != artifact_name);

    mender_err_t ret  = MENDER_FAIL;
    char        *path = NULL;

    /* Compute path */
    if (-1 == asprintf(&path, MENDER_API_PATH_GET_NEXT_DEPLOYMENT "?artifact_name=%s&device_type=%s", artifact_name, mender_api_config.device_type)) {
        mender_log_error("Unable to allocate memory");
        goto END;
    }

    /* Perform HTTP request */
    if (MENDER_OK != (ret = mender_http_perform(mender_api_jwt, path, MENDER_HTTP_GET, NULL, NULL, &mender_api_http_text_callback, (void *)response, status))) {
        mender_log_error("Unable to perform HTTP request");
        goto END;
    }

    ret = MENDER_OK;

END:

    /* Release memory */
    free(path);

    return ret;
}

mender_err_t
mender_api_check_for_deployment(mender_api_deployment_data_t *deployment) {

    assert(NULL != deployment);
    mender_err_t ret      = MENDER_FAIL;
    char        *response = NULL;
    int          status   = 0;

    if (MENDER_FAIL == (ret = api_check_for_deployment_v2(&status, (void *)&response))) {
        goto END;
    }

    /* Yes, 404 still means MENDER_OK above */
    if (404 == status) {
        mender_log_debug("POST request to v2 version of the deployments API failed, falling back to v1 version and GET");
        free(response);
        response = NULL;
        if (MENDER_FAIL == (ret = api_check_for_deployment_v1(&status, (void *)&response))) {
            goto END;
        }
    }

    /* Treatment depending of the status */
    if (200 == status) {
        cJSON *json_response = cJSON_Parse(response);
        if (NULL != json_response) {
            cJSON *json_id = cJSON_GetObjectItem(json_response, "id");
            if (NULL != json_id) {
                if (NULL == (deployment->id = strdup(cJSON_GetStringValue(json_id)))) {
                    ret = MENDER_FAIL;
                    goto END;
                }
            }
            cJSON *json_artifact = cJSON_GetObjectItem(json_response, "artifact");
            if (NULL != json_artifact) {
                cJSON *json_artifact_name = cJSON_GetObjectItem(json_artifact, "artifact_name");
                if (NULL != json_artifact_name) {
                    if (NULL == (deployment->artifact_name = strdup(cJSON_GetStringValue(json_artifact_name)))) {
                        ret = MENDER_FAIL;
                        goto END;
                    }
                }
                cJSON *json_source = cJSON_GetObjectItem(json_artifact, "source");
                if (NULL != json_source) {
                    cJSON *json_uri = cJSON_GetObjectItem(json_source, "uri");
                    if (NULL != json_uri) {
                        if (NULL == (deployment->uri = strdup(cJSON_GetStringValue(json_uri)))) {
                            ret = MENDER_FAIL;
                            goto END;
                        }
                        ret = MENDER_OK;
                    } else {
                        mender_log_error("Invalid response");
                        ret = MENDER_FAIL;
                    }
                } else {
                    mender_log_error("Invalid response");
                    ret = MENDER_FAIL;
                }
                cJSON *json_device_types_compatible = cJSON_GetObjectItem(json_artifact, "device_types_compatible");
                if (NULL != json_device_types_compatible && cJSON_IsArray(json_device_types_compatible)) {
                    deployment->device_types_compatible_size = cJSON_GetArraySize(json_device_types_compatible);
                    deployment->device_types_compatible      = (char **)malloc(deployment->device_types_compatible_size * sizeof(char *));
                    if (NULL == deployment->device_types_compatible) {
                        mender_log_error("Unable to allocate memory");
                        ret = MENDER_FAIL;
                        goto END;
                    }
                    for (size_t i = 0; i < deployment->device_types_compatible_size; i++) {
                        cJSON *json_device_type = cJSON_GetArrayItem(json_device_types_compatible, i);
                        if (NULL != json_device_type && cJSON_IsString(json_device_type)) {
                            if (NULL == (deployment->device_types_compatible[i] = strdup(cJSON_GetStringValue(json_device_type)))) {
                                ret = MENDER_FAIL;
                                goto END;
                            }
                        } else {
                            mender_log_error("Could not get device type form device_types_compatible array");
                            ret = MENDER_FAIL;
                        }
                    }
                } else {
                    mender_log_error("Could not load device_types_compatible");
                    ret = MENDER_FAIL;
                }
            } else {
                mender_log_error("Invalid response");
                ret = MENDER_FAIL;
            }
            cJSON_Delete(json_response);
        } else {
            mender_log_error("Invalid response");
            ret = MENDER_FAIL;
        }
    } else if (204 == status) {
        /* No response expected */
        ret = MENDER_NOT_FOUND;
    } else {
        mender_api_print_response_error(response, status);
        ret = MENDER_FAIL;
    }

END:

    /* Release memory */
    free(response);

    return ret;
}

mender_err_t
mender_api_publish_deployment_status(const char *id, mender_deployment_status_t deployment_status) {

    assert(NULL != id);
    mender_err_t ret;
    char        *value        = NULL;
    cJSON       *json_payload = NULL;
    char        *payload      = NULL;
    char        *path         = NULL;
    char        *response     = NULL;
    int          status       = 0;

    /* Deployment status to string */
    if (NULL == (value = mender_utils_deployment_status_to_string(deployment_status))) {
        mender_log_error("Invalid status");
        ret = MENDER_FAIL;
        goto END;
    }

    /* Format payload */
    if (NULL == (json_payload = cJSON_CreateObject())) {
        mender_log_error("Unable to allocate memory");
        ret = MENDER_FAIL;
        goto END;
    }
    cJSON_AddStringToObject(json_payload, "status", value);
    if (NULL == (payload = cJSON_PrintUnformatted(json_payload))) {
        mender_log_error("Unable to allocate memory");
        ret = MENDER_FAIL;
        goto END;
    }

    /* Compute path */
    size_t str_length = strlen(MENDER_API_PATH_PUT_DEPLOYMENT_STATUS) - strlen("%s") + strlen(id) + 1;
    if (NULL == (path = (char *)malloc(str_length))) {
        mender_log_error("Unable to allocate memory");
        ret = MENDER_FAIL;
        goto END;
    }
    snprintf(path, str_length, MENDER_API_PATH_PUT_DEPLOYMENT_STATUS, id);

    /* Perform HTTP request */
    if (MENDER_OK
        != (ret = mender_http_perform(mender_api_jwt, path, MENDER_HTTP_PUT, payload, NULL, &mender_api_http_text_callback, (void *)&response, &status))) {
        mender_log_error("Unable to perform HTTP request");
        goto END;
    }

    /* Treatment depending of the status */
    if (204 == status) {
        /* No response expected */
        ret = MENDER_OK;
    } else {
        mender_api_print_response_error(response, status);
        ret = MENDER_FAIL;
    }

END:

    /* Release memory */
    free(response);
    free(path);
    free(payload);
    cJSON_Delete(json_payload);

    return ret;
}

mender_err_t
mender_api_download_artifact(char *uri, mender_err_t (*callback)(char *, cJSON *, char *, size_t, void *, size_t, size_t)) {

    assert(NULL != uri);
    assert(NULL != callback);
    mender_err_t ret;
    int          status = 0;

    /* Perform HTTP request */
    if (MENDER_OK != (ret = mender_http_perform(NULL, uri, MENDER_HTTP_GET, NULL, NULL, &mender_api_http_artifact_callback, callback, &status))) {
        mender_log_error("Unable to perform HTTP request");
        goto END;
    }

    /* Treatment depending of the status */
    if (200 == status) {
        /* Nothing to do */
        ret = MENDER_OK;
    } else {
        mender_api_print_response_error(NULL, status);
        ret = MENDER_FAIL;
    }

END:

    return ret;
}

#ifdef CONFIG_MENDER_CLIENT_INVENTORY

mender_err_t
mender_api_publish_inventory_data(mender_keystore_t *inventory) {

    assert(NULL != artifact_name);

    mender_err_t ret;
    char        *payload  = NULL;
    char        *response = NULL;
    int          status   = 0;

    /* Format payload */
    cJSON *object = cJSON_CreateArray();
    if (NULL == object) {
        mender_log_error("Unable to allocate memory");
        ret = MENDER_FAIL;
        goto END;
    }
    cJSON *item = cJSON_CreateObject();
    if (NULL == item) {
        mender_log_error("Unable to allocate memory");
        ret = MENDER_FAIL;
        goto END;
    }
    cJSON_AddStringToObject(item, "name", "artifact_name");
    cJSON_AddStringToObject(item, "value", artifact_name);
    cJSON_AddItemToArray(object, item);
    item = cJSON_CreateObject();
    if (NULL == item) {
        mender_log_error("Unable to allocate memory");
        ret = MENDER_FAIL;
        goto END;
    }
    cJSON_AddStringToObject(item, "name", "rootfs-image.version");
    cJSON_AddStringToObject(item, "value", artifact_name);
    cJSON_AddItemToArray(object, item);
    item = cJSON_CreateObject();
    if (NULL == item) {
        mender_log_error("Unable to allocate memory");
        ret = MENDER_FAIL;
        goto END;
    }
    cJSON_AddStringToObject(item, "name", "device_type");
    cJSON_AddStringToObject(item, "value", mender_api_config.device_type);
    cJSON_AddItemToArray(object, item);
    if (NULL != inventory) {
        size_t index = 0;
        while ((NULL != inventory[index].name) && (NULL != inventory[index].value)) {
            if (NULL == (item = cJSON_CreateObject())) {
                mender_log_error("Unable to allocate memory");
                ret = MENDER_FAIL;
                goto END;
            }
            cJSON_AddStringToObject(item, "name", inventory[index].name);
            cJSON_AddStringToObject(item, "value", inventory[index].value);
            cJSON_AddItemToArray(object, item);
            index++;
        }
    }
    if (NULL == (payload = cJSON_PrintUnformatted(object))) {
        mender_log_error("Unable to allocate memory");
        ret = MENDER_FAIL;
        goto END;
    }

    /* Perform HTTP request */
    if (MENDER_OK
        != (ret = mender_http_perform(mender_api_jwt,
                                      MENDER_API_PATH_PUT_DEVICE_ATTRIBUTES,
                                      MENDER_HTTP_PUT,
                                      payload,
                                      NULL,
                                      &mender_api_http_text_callback,
                                      (void *)&response,
                                      &status))) {
        mender_log_error("Unable to perform HTTP request");
        goto END;
    }

    /* Treatment depending of the status */
    if (200 == status) {
        /* No response expected */
        ret = MENDER_OK;
    } else {
        mender_api_print_response_error(response, status);
        ret = MENDER_FAIL;
    }

END:

    /* Release memory */
    free(response);
    free(payload);
    cJSON_Delete(object);

    return ret;
}

#endif /* CONFIG_MENDER_CLIENT_INVENTORY */

mender_err_t
mender_api_exit(void) {

    /* Release all modules */
    mender_http_exit();

    /* Release memory */
    free(mender_api_jwt);
    mender_api_jwt = NULL;
    free(artifact_name);
    artifact_name = NULL;

    return MENDER_OK;
}

static mender_err_t
mender_api_http_text_callback(mender_http_client_event_t event, void *data, size_t data_length, void *params) {

    assert(NULL != params);
    char       **response = (char **)params;
    mender_err_t ret      = MENDER_OK;
    char        *tmp;

    /* Treatment depending of the event */
    switch (event) {
        case MENDER_HTTP_EVENT_CONNECTED:
            /* Nothing to do */
            break;
        case MENDER_HTTP_EVENT_DATA_RECEIVED:
            /* Check input data */
            if ((NULL == data) || (0 == data_length)) {
                mender_log_error("Invalid data received");
                ret = MENDER_FAIL;
                break;
            }
            /* Concatenate data to the response */
            size_t response_length = (NULL != *response) ? strlen(*response) : 0;
            if (NULL == (tmp = realloc(*response, response_length + data_length + 1))) {
                mender_log_error("Unable to allocate memory");
                ret = MENDER_FAIL;
                break;
            }
            *response = tmp;
            memcpy((*response) + response_length, data, data_length);
            *((*response) + response_length + data_length) = '\0';
            break;
        case MENDER_HTTP_EVENT_DISCONNECTED:
            /* Nothing to do */
            break;
        case MENDER_HTTP_EVENT_ERROR:
            /* Downloading the response fails */
            mender_log_error("An error occurred");
            ret = MENDER_FAIL;
            break;
        default:
            /* Should no occur */
            ret = MENDER_FAIL;
            break;
    }

    return ret;
}

static mender_err_t
mender_api_http_artifact_callback(mender_http_client_event_t event, void *data, size_t data_length, void *params) {

    assert(NULL != params);
    mender_err_t ret = MENDER_OK;

    mender_artifact_ctx_t *mender_artifact_ctx = NULL;

    /* Treatment depending of the event */
    switch (event) {
        case MENDER_HTTP_EVENT_CONNECTED:
            /* Create new artifact context */
            if (NULL == (mender_artifact_ctx = mender_artifact_create_ctx())) {
                mender_log_error("Unable to create artifact context");
                ret = MENDER_FAIL;
                break;
            }
            break;
        case MENDER_HTTP_EVENT_DATA_RECEIVED:
            /* Check input data */
            if ((NULL == data) || (0 == data_length)) {
                mender_log_error("Invalid data received");
                ret = MENDER_FAIL;
                break;
            }

            /* Check artifact context */
            if (MENDER_OK != mender_artifact_get_ctx(&mender_artifact_ctx)) {
                mender_log_error("Unable to get artifact context");
                ret = MENDER_FAIL;
                break;
            }
            assert(NULL != mender_artifact_ctx);

            /* Parse input data */
            if (MENDER_OK != (ret = mender_artifact_process_data(mender_artifact_ctx, data, data_length, params))) {
                mender_log_error("Unable to process data");
                break;
            }
            break;
        case MENDER_HTTP_EVENT_DISCONNECTED:
            break;
        case MENDER_HTTP_EVENT_ERROR:
            /* Downloading the artifact fails */
            mender_log_error("An error occurred");
            ret = MENDER_FAIL;
            break;
        default:
            /* Should not occur */
            ret = MENDER_FAIL;
            break;
    }

    return ret;
}

static void
mender_api_print_response_error(char *response, int status) {

    char *desc;

    /* Treatment depending of the status */
    if (NULL != (desc = mender_utils_http_status_to_string(status))) {
        if (NULL != response) {
            cJSON *json_response = cJSON_Parse(response);
            if (NULL != json_response) {
                cJSON *json_error = cJSON_GetObjectItemCaseSensitive(json_response, "error");
                if (NULL != json_error) {
                    mender_log_error("[%d] %s: %s", status, desc, cJSON_GetStringValue(json_error));
                } else {
                    mender_log_error("[%d] %s: unknown error", status, desc);
                }
                cJSON_Delete(json_response);
            } else {
                mender_log_error("[%d] %s: unknown error", status, desc);
            }
        } else {
            mender_log_error("[%d] %s: unknown error", status, desc);
        }
    } else {
        mender_log_error("Unknown error occurred, status=%d", status);
    }
}
