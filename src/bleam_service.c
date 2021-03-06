/** @file bleam_service.h
 *
 * @defgroup bleam_service BLEAM service client
 * @{
 * @ingroup bleam_connect
 *
 * @brief BLE service for BLEAM interaction
 */

#include "bleam_service.h"
#include "log.h"
#include "sdk_common.h"

static bool bleam_service_client_initialized = false; /**< Flag denoting whether BLEAM service was initialized or not. */
static bleam_service_client_mode_type_t m_bleam_service_mode = BLEAM_SERVICE_CLIENT_MODE_NONE; /**< BLEAM Service mode of BLEAM interation. */

/**@brief Function for handling the Disconnect event.
 *
 * @param[in]   p_bleam_service_client       Pointer to the struct of BLEAM service.
 * @param[in]   p_ble_evt                    Pointer to event received from the BLE stack.
 *
 * @returns Nothing.
 */
static void on_disconnect(bleam_service_client_t *p_bleam_service_client, ble_evt_t const *p_ble_evt) {
    switch (p_ble_evt->evt.gap_evt.params.disconnected.reason) {
    case BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION:
        //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "REMOTE USER terminated connection\r\n");
        break;
    case BLE_HCI_LOCAL_HOST_TERMINATED_CONNECTION:
        //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "LOCAL HOST terminated connection\r\n");
        break;
    case BLE_HCI_CONN_FAILED_TO_BE_ESTABLISHED:
        //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "CONNECTION FAILED to be established\r\n");
        break;
    }

    p_bleam_service_client->conn_handle = BLE_CONN_HANDLE_INVALID;

    bleam_service_client_evt_t evt;

    evt.evt_type = BLEAM_SERVICE_CLIENT_EVT_DISCONNECTED;

    p_bleam_service_client->evt_handler(p_bleam_service_client, &evt);
}

/**@brief     Function for handling notification received from the SoftDevice.
 *
 * @details   This function gets the notification received from the SoftDevice
 *            and checks if it is a notification of the NOTIFY characteristic from the peer. If
 *            it is, this function will decode the data and send it to the
 *            application.
 *
 * @param[in] p_bleam_service_client Pointer to the BLEAM service client structure.
 * @param[in] p_ble_evt              Pointer to the BLE event received.
 *
 * @returns Nothing.
 */
static void on_hvx(bleam_service_client_t *p_bleam_service_client, ble_evt_t const *p_ble_evt) {
    // HVX can only occur from client sending.
    if ((p_bleam_service_client->handles.salt_handle != BLE_GATT_HANDLE_INVALID) && (p_ble_evt->evt.gattc_evt.params.hvx.handle == p_bleam_service_client->handles.salt_handle) && (p_bleam_service_client->evt_handler != NULL)) {
        bleam_service_client_evt_t evt;

        evt.evt_type = BLEAM_SERVICE_CLIENT_EVT_RECV_SALT;
        evt.p_data = (uint8_t *)p_ble_evt->evt.gattc_evt.params.hvx.data;
        evt.data_len = p_ble_evt->evt.gattc_evt.params.hvx.len;

        p_bleam_service_client->evt_handler(p_bleam_service_client, &evt);
    }
}

/**@brief     Function for handling read request response from peer.
 *
 * @details   This function checks if it is a read data from the TIME characteristic of the peer.
 *            If it is, this function will decode the data and send it to the application.
 *
 * @param[in] p_bleam_service_client Pointer to the BLEAM service client structure.
 * @param[in] p_ble_evt              Pointer to the BLE event received.
 *
 * @returns Nothing.
 */
static void on_read(bleam_service_client_t *p_bleam_service_client, ble_evt_t const *p_ble_evt) {
    if ((p_bleam_service_client->handles.time_handle != BLE_GATT_HANDLE_INVALID) && (p_ble_evt->evt.gattc_evt.params.read_rsp.handle == p_bleam_service_client->handles.time_handle) && (p_bleam_service_client->evt_handler != NULL)) {
        bleam_service_client_evt_t evt;

        evt.evt_type = BLEAM_SERVICE_CLIENT_EVT_RECV_TIME;
        evt.p_data = (uint8_t *)p_ble_evt->evt.gattc_evt.params.read_rsp.data;
        evt.data_len = p_ble_evt->evt.gattc_evt.params.read_rsp.len;

        p_bleam_service_client->evt_handler(p_bleam_service_client, &evt);
    }
}

uint32_t bleam_service_client_init(bleam_service_client_t *p_bleam_service_client, bleam_service_client_init_t *p_bleam_service_client_init) {
    uint32_t err_code;
    ble_uuid_t bleam_service_uuid;
    ble_uuid128_t bleam_service_base_uuid = {BLE_UUID_BLEAM_SERVICE_BASE_UUID};

    if (p_bleam_service_client == NULL || p_bleam_service_client_init == NULL || p_bleam_service_client_init->evt_handler == NULL)
        return NRF_ERROR_NULL;

    p_bleam_service_client->handles.salt_handle = BLE_GATT_HANDLE_INVALID;
    p_bleam_service_client->handles.signature_handle = BLE_GATT_HANDLE_INVALID;
    p_bleam_service_client->handles.rssi_handle = BLE_GATT_HANDLE_INVALID;
    p_bleam_service_client->handles.health_handle = BLE_GATT_HANDLE_INVALID;
    p_bleam_service_client->conn_handle = BLE_CONN_HANDLE_INVALID;
    p_bleam_service_client->evt_handler = p_bleam_service_client_init->evt_handler;

    err_code = sd_ble_uuid_vs_add(&bleam_service_base_uuid, &p_bleam_service_client->uuid_type);
    APP_ERROR_CHECK(err_code);

    bleam_service_uuid.type = p_bleam_service_client->uuid_type;
    bleam_service_uuid.uuid = BLEAM_SERVICE_UUID;
    err_code = ble_db_discovery_evt_register(&bleam_service_uuid);
    if(NRF_SUCCESS == err_code)
        bleam_service_client_initialized = true;
    return err_code;
}

uint32_t bleam_service_uuid_vs_replace(bleam_service_client_t *p_bleam_service_client, ble_uuid128_t *bleam_service_base_uuid) {
    uint32_t err_code;
    err_code = sd_ble_uuid_vs_remove(NULL);
    if (NRF_SUCCESS == err_code)
        err_code = sd_ble_uuid_vs_add(bleam_service_base_uuid, &p_bleam_service_client->uuid_type);
    return err_code;
}

void bleam_service_client_on_ble_evt(ble_evt_t const *p_ble_evt, void *p_context) {
    if (!bleam_service_client_initialized || (p_context == NULL) || (p_ble_evt == NULL)) {
        return;
    }
    bleam_service_client_t *p_bleam_service_client = (bleam_service_client_t *)p_context;
    switch (p_ble_evt->header.evt_id) {
    case BLE_GATTC_EVT_HVX:
        //        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "HVX event\r\n");
        on_hvx(p_bleam_service_client, p_ble_evt);
        break;
    case BLE_GATTC_EVT_READ_RSP:
        on_read(p_bleam_service_client, p_ble_evt);
        break;
    case BLE_GAP_EVT_DISCONNECTED:
        on_disconnect(p_bleam_service_client, p_ble_evt);
        break;
    default:
        break;
    }
}

void bleam_service_on_db_disc_evt(bleam_service_client_t *p_bleam_service_client, const ble_db_discovery_evt_t *p_evt) {
    //    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Discovery event %d (%d) 0x%04x\r\n", p_evt->evt_type, BLE_DB_DISCOVERY_COMPLETE, p_evt->params.discovered_db.srv_uuid.uuid);
    // Check if the BLEAM Service was discovered.
    if (BLE_DB_DISCOVERY_COMPLETE  == p_evt->evt_type && BLEAM_SERVICE_UUID == p_evt->params.discovered_db.srv_uuid.uuid) {
        bleam_service_client_evt_t evt = {0};
        evt.evt_type = BLEAM_SERVICE_CLIENT_EVT_DISCOVERY_COMPLETE;
        evt.conn_handle = p_evt->conn_handle;
        for (uint32_t i = 0; i < p_evt->params.discovered_db.char_count; i++) {
            const ble_gatt_db_char_t *p_char = &(p_evt->params.discovered_db.charateristics[i]);
            switch (p_char->characteristic.uuid.uuid) {
            case BLEAM_S_NOTIFY:
                //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Found notify characteristic %04X\r\n", p_char->characteristic.uuid.uuid);
                evt.handles.salt_handle = p_char->characteristic.handle_value;
                evt.handles.salt_cccd_handle = p_char->cccd_handle;
                break;
            case BLEAM_S_SIGN:
                //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Found signature characteristic %04X\r\n", p_char->characteristic.uuid.uuid);
                evt.handles.signature_handle = p_char->characteristic.handle_value;
                break;
            case BLEAM_S_RSSI:
                //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Found RSSI characteristic %04X\r\n", p_char->characteristic.uuid.uuid);
                evt.handles.rssi_handle = p_char->characteristic.handle_value;
                break;
            case BLEAM_S_HEALTH:
                //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Found health characteristic %04X\r\n", p_char->characteristic.uuid.uuid);
                evt.handles.health_handle = p_char->characteristic.handle_value;
                break;
            case BLEAM_S_TIME:
                //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Found time characteristic %04X\r\n", p_char->characteristic.uuid.uuid);
                evt.handles.time_handle = p_char->characteristic.handle_value;
                break;
            default:
                //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Found unknown characteristic %04X\r\n", p_char->characteristic.uuid.uuid);
                break;
            }
        }
        //__LOG(LOG_SRC_APP, LOG_LEVEL_DBG1, "BLEAM Service discovered at peer.");
        //If the instance has been assigned prior to db_discovery, assign the db_handles
        if (p_bleam_service_client->conn_handle != BLE_CONN_HANDLE_INVALID) {
            memcpy(&p_bleam_service_client->handles, &evt.handles, sizeof(evt.handles));
        }
        p_bleam_service_client->evt_handler(p_bleam_service_client, &evt);
    } else if (BLE_DB_DISCOVERY_SRV_NOT_FOUND == p_evt->evt_type) {
        bleam_service_client_evt_t evt = {
            .evt_type = BLEAM_SERVICE_CLIENT_EVT_SRV_NOT_FOUND,
            .conn_handle = p_evt->conn_handle,
        };
        p_bleam_service_client->evt_handler(p_bleam_service_client, &evt);
    } else {
        bleam_service_client_evt_t evt = {
            .evt_type = BLEAM_SERVICE_CLIENT_EVT_BAD_CONNECTION,
            .conn_handle = p_evt->conn_handle,
        };
        p_bleam_service_client->evt_handler(p_bleam_service_client, &evt);
    }
}

uint32_t bleam_service_client_handles_assign(bleam_service_client_t *p_bleam_service_client, uint16_t conn_handle, const bleam_service_db_t *p_peer_handles) {
    if (p_bleam_service_client == NULL)
        return NRF_ERROR_NULL;
    p_bleam_service_client->conn_handle = conn_handle;
    if (p_peer_handles != NULL) {
        p_bleam_service_client->handles = *p_peer_handles;
    }
    return NRF_SUCCESS;
}

uint32_t bleam_service_client_notify_enable(bleam_service_client_t *p_bleam_service_client) {
    VERIFY_PARAM_NOT_NULL(p_bleam_service_client);

    if ((p_bleam_service_client->conn_handle == BLE_CONN_HANDLE_INVALID) ||
        (p_bleam_service_client->handles.salt_cccd_handle == BLE_GATT_HANDLE_INVALID)) {
        //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "%d %d\r\n", p_bleam_service_client->conn_handle == BLE_CONN_HANDLE_INVALID,
        //                                     p_bleam_service_client->handles.salt_cccd_handle == BLE_GATT_HANDLE_INVALID);
        return NRF_ERROR_INVALID_STATE;
    }

    uint8_t buf[BLE_CCCD_VALUE_LEN];

    buf[0] = BLE_GATT_HVX_NOTIFICATION;
    buf[1] = 0;

    ble_gattc_write_params_t const write_params = {
        .write_op = BLE_GATT_OP_WRITE_REQ,
        .flags = BLE_GATT_EXEC_WRITE_FLAG_PREPARED_WRITE,
        .handle = p_bleam_service_client->handles.salt_cccd_handle,
        .offset = 0,
        .len = sizeof(buf),
        .p_value = buf};

    return sd_ble_gattc_write(p_bleam_service_client->conn_handle, &write_params);
}

uint32_t bleam_service_client_read_time(bleam_service_client_t *p_bleam_service_client) {
    VERIFY_PARAM_NOT_NULL(p_bleam_service_client);
    if (p_bleam_service_client->conn_handle == BLE_CONN_HANDLE_INVALID) {
        return NRF_ERROR_INVALID_STATE;
    }
    if (p_bleam_service_client->handles.time_handle == BLE_GATT_HANDLE_INVALID) {
        return NRF_ERROR_NOT_FOUND;
    }
    return sd_ble_gattc_read(p_bleam_service_client->conn_handle, p_bleam_service_client->handles.time_handle, 0);
}

uint32_t bleam_service_data_send(bleam_service_client_t *p_bleam_service_client, uint8_t *data_array, uint16_t *data_size, uint16_t write_handle) {
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Update data on handle 0x%04X\r\n", write_handle);
    if (data_array == NULL)
        return NRF_ERROR_NULL;
    __LOG_XB(LOG_SRC_APP, LOG_LEVEL_INFO, "Data", data_array, *data_size);

    if (p_bleam_service_client == NULL)
        return NRF_ERROR_NULL;

    if (*data_size > BLEAM_MAX_DATA_LEN) {
        //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Content too long.\r\n");
        return NRF_ERROR_INVALID_PARAM;
    }
    if (p_bleam_service_client->conn_handle == BLE_CONN_HANDLE_INVALID) {
        //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Connection handle invalid.\r\n");
        return NRF_ERROR_INVALID_STATE;
    }

    // Snippet to transform write_handle value from characteristic UUID bleam_service_char_t
    // to corresponding peer characteristic handle.
    switch (write_handle) {
    case BLEAM_S_RSSI:
        write_handle = p_bleam_service_client->handles.rssi_handle;
        break;
    case BLEAM_S_HEALTH:
        write_handle = p_bleam_service_client->handles.health_handle;
        break;
    case BLEAM_S_SIGN:
        write_handle = p_bleam_service_client->handles.signature_handle;
        break;
    }

    ble_gattc_write_params_t const write_params =
        {
            .write_op = BLE_GATT_OP_WRITE_CMD,
            .flags = BLE_GATT_EXEC_WRITE_FLAG_PREPARED_WRITE,
            .handle = write_handle,
            .offset = 0,
            .len = *data_size,
            .p_value = data_array};

    return sd_ble_gattc_write(p_bleam_service_client->conn_handle, &write_params);
}

bleam_service_client_mode_type_t bleam_service_mode_get(void) {
    return m_bleam_service_mode;
}

void bleam_service_mode_set(bleam_service_client_mode_type_t p_mode) {
    m_bleam_service_mode = p_mode;
}

/** @}*/