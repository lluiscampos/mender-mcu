/**
 * @file      mender-artifact-download.c
 * @brief     Mender artifact download implementation
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

#include "mender-api.h"
#include "mender-artifact.h"
#include "mender-artifact-download.h"
#include "mender-artifact-download-data.h"
#include "mender-http.h"
#include "mender-log.h"

static mender_err_t mender_download_artifact_callback(mender_http_client_event_t       event,
                                                      void                            *data,
                                                      size_t                           data_length,
                                                      mender_artifact_download_data_t *dl_data);

mender_err_t
mender_download_artifact(char *uri, mender_deployment_data_t *deployment_data, mender_update_module_t **update_module) {
    assert(NULL != uri);
    assert(NULL != deployment_data);
    assert(NULL != update_module);

    mender_err_t ret;
    int          status = 0;

    mender_artifact_download_data_t dl_data = {
        .deployment                 = deployment_data,
        .artifact_download_callback = &mender_download_artifact_callback,
        .ret                        = MENDER_OK,
    };

    /* Perform HTTP request */
    if (MENDER_OK != (ret = mender_http_artifact_download(uri, &dl_data, &status))) {
        mender_log_error("Unable to perform HTTP request");
        goto END;
    }

    /* Treatment depending of the status */
    if (200 == status) {
        /* Nothing to do */
        ret            = MENDER_OK;
        *update_module = dl_data.update_module;
    } else {
        mender_api_print_response_error(NULL, status);
        ret = MENDER_FAIL;
    }

END:

    return ret;
}

static mender_err_t
mender_download_artifact_callback(mender_http_client_event_t event, void *data, size_t data_length, mender_artifact_download_data_t *dl_data) {
    mender_err_t ret = MENDER_OK;

    mender_artifact_ctx_t *mender_artifact_ctx = NULL;

    /* Treatment depending of the event */
    switch (event) {
        case MENDER_HTTP_EVENT_CONNECTED:
            /* Create new artifact context */
            if (NULL == (mender_artifact_ctx = mender_artifact_create_ctx(2 * MENDER_ARTIFACT_STREAM_BLOCK_SIZE + mender_http_recv_buf_length))) {
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
            if (MENDER_OK != (ret = mender_artifact_process_data(mender_artifact_ctx, data, data_length, dl_data))) {
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
