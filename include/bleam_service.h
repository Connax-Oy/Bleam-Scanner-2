/**
 * @addtogroup bleam_service
 * @{
 */

#ifndef BLEAM_SERVICE_H__
#define BLEAM_SERVICE_H__

#include <stdint.h>
#include <stdbool.h>
#include "blesc_error.h"
#include "ble.h"
#include "ble_srv_common.h"
#include "ble_db_discovery.h"
#include "nrf_sdh_ble.h"
#include "global_app_config.h"
#include "app_timer.h"

/** Forward declaration of the @ref bleam_service_client_s type. */
typedef struct bleam_service_client_s bleam_service_client_t;

/** Base UUID for BLEAMs */
#define APP_CONFIG_BLEAM_SERVICE_BASE_UUID 


#define BLEAM_SERVICE_UUID                APP_CONFIG_BLEAM_SERVICE_UUID          /**< UUID of BLEAM device. */
#define BLE_UUID_BLEAM_SERVICE_BASE_UUID {0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, \
                                          0x00, 0x00, \
                                          0x00, 0x00, \
                                          0x00, 0x00, \
                                          (uint8_t)(0xFF & BLEAM_SERVICE_UUID), (uint8_t)(BLEAM_SERVICE_UUID >> 8), \
                                          0x00, 0x00}                            /**< Base UUID for BLEAMs */
#define BLEAM_SERVICE_BLEAM_INACTIVITY_TIMEOUT APP_TIMER_TICKS(APP_CONFIG_BLEAM_INACTIVITY_TIMEOUT) /**< Maximum time of inactivity of BLEAM interaction, ticks. */

/**@brief Provisioning Service characterisctic IDs.
  *
  *@note Members of this enumeration start counting up after @ref BLEAM_SERVICE_UUID
  */
typedef enum {
    BLEAM_S_NOTIFY = BLEAM_SERVICE_UUID + 1, /**< Event + Data. [NOTIFY,READ] */
    BLEAM_S_SIGN,                            /**< Signature or Salt. [WRITE] */
    BLEAM_S_RSSI,                            /**< RSSI data characteristic. [WRITE] */
    BLEAM_S_HEALTH,                          /**< Health status characteristic. [WRITE] */
    BLEAM_S_TIME,                            /**< Local BLEAM time. [READ] */
} bleam_service_char_t;

#define BLEAM_MAX_DATA_LEN                     (NRF_SDH_BLE_GATT_MAX_MTU_SIZE - 3) /**< Maximum length of data to send to BLEAM */
#define BLEAM_SERVICE_CLIENT_BLE_OBSERVER_PRIO 2                                   /**< Service's BLE observer priority. */

#define BLEAM_SERVICE_CLIENT_DEF(_name)         \
    static bleam_service_client_t _name;        \
    NRF_SDH_BLE_OBSERVER(_name##_obs,           \
        BLEAM_SERVICE_CLIENT_BLE_OBSERVER_PRIO, \
        bleam_service_client_on_ble_evt, &_name) /**< Macro for BLEAM service definition and registering observer. */

/**@brief BLEAM Service event type. */
typedef enum {
    BLEAM_SERVICE_CLIENT_EVT_NOTIFICATION_ENABLED,   /**< Notification enabled event. */
    BLEAM_SERVICE_CLIENT_EVT_NOTIFICATION_DISABLED,  /**< Notification disabled event. */
    BLEAM_SERVICE_CLIENT_EVT_DISCOVERY_COMPLETE,     /**< Discovered at the peer event. */
    BLEAM_SERVICE_CLIENT_EVT_SRV_NOT_FOUND,          /**< Service is not found at the peer event. */
    BLEAM_SERVICE_CLIENT_EVT_BAD_CONNECTION,         /**< Service discovery failed, or other connecion error to the peer event. */
    BLEAM_SERVICE_CLIENT_EVT_DISCONNECTED,           /**< Disconnected from the peer event. */
    BLEAM_SERVICE_CLIENT_EVT_CONNECTED,              /**< Connected to the peer event. */
    BLEAM_SERVICE_CLIENT_EVT_RECV_SALT,              /**< Read salt from peer event. */
    BLEAM_SERVICE_CLIENT_EVT_RECV_TIME,              /**< Read time from peer event. */
    BLEAM_SERVICE_CLIENT_EVT_DONE_SENDING_SIGNATURE, /**< Done sending signature to peer event. */
    BLEAM_SERVICE_CLIENT_EVT_DONE_SENDING,           /**< Done sending data to peer event. */
} bleam_service_client_evt_type_t;

/**@brief BLEAM Service mode type, signifying protocol of BLEAM interaction. */
typedef enum {
    BLEAM_SERVICE_CLIENT_MODE_NONE,     /**< No mode has been set for this connection. */
    BLEAM_SERVICE_CLIENT_MODE_RSSI,     /**< Generic send-RSSI-to-BLEAM interaction. */
    BLEAM_SERVICE_CLIENT_MODE_DFU,      /**< Confirm BLEAM is genuine and enter DFU. */
    BLEAM_SERVICE_CLIENT_MODE_REBOOT,   /**< Confirm BLEAM is genuine and reboot BLEAM Scanner. */
    BLEAM_SERVICE_CLIENT_MODE_UNCONFIG, /**< Confirm BLEAM is genuine and remove configuration data from BLEAM Scanner. */
} bleam_service_client_mode_type_t;

/**@brief BLEAM Service command type, value received within the salt package. */
typedef enum {
    BLEAM_SERVICE_CLIENT_CMD_SALT,     /**< Received salt for BLEAM RSSI interaction, ready to accept signature from BLEAM Scanner. */
    BLEAM_SERVICE_CLIENT_CMD_DFU,      /**< Received command for entering DFU, ready to accept salt from BLEAM Scanner. */
    BLEAM_SERVICE_CLIENT_CMD_SIGN1,    /**< Received the first half of 32-byte signature from BLEAM. */
    BLEAM_SERVICE_CLIENT_CMD_SIGN2,    /**< Received the second half of 32-byte signature from BLEAM. */
    BLEAM_SERVICE_CLIENT_CMD_REBOOT,   /**< Received command for node reboot, ready to accept salt from BLEAM Scanner. */
    BLEAM_SERVICE_CLIENT_CMD_UNCONFIG, /**< Received command for node unconfiguration, ready to accept salt from BLEAM Scanner. */
} bleam_service_client_cmd_type_t;

/**@brief Structure containing the handles related to the BLEAM Service found on the peer. */
typedef struct {
    uint16_t salt_handle;      /**< Handle of the NOTIFY read characteristic as provided by the SoftDevice. */
    uint16_t salt_cccd_handle; /**< Handle of the NOTIFY notify characteristic as provided by the SoftDevice. */
    uint16_t signature_handle; /**< Handle of the SIGN characteristic as provided by the SoftDevice. */
    uint16_t rssi_handle;      /**< Handle of the RSSI characteristic as provided by the SoftDevice. */
    uint16_t health_handle;    /**< Handle of the HEALTH characteristic as provided by the SoftDevice. */
    uint16_t time_handle;      /**< Handle of the TIME characteristic as provided by the SoftDevice. */
} bleam_service_db_t;

/**@brief BLEAM Event structure. */
typedef struct {
    bleam_service_client_evt_type_t evt_type; /**< Type of the event. */
    uint16_t conn_handle;                     /**< Connection handle on which the event occured.*/
    bleam_service_db_t handles;               /**< Handles found on the peer device. This will be filled if the evt_type is @ref BLEAM_SERVICE_CLIENT_EVT_DISCOVERY_COMPLETE.*/
    uint16_t data_len;                        /**< Length of data received. This will be filled if the ext_type is @ref BLEAM_SERVICE_CLIENT_EVT_RECV_SALT or @ref BLEAM_SERVICE_CLIENT_EVT_RECV_TIME. */
    uint8_t *p_data;                          /**< Data received. This will be filled if the ext_type is @ref BLEAM_SERVICE_CLIENT_EVT_RECV_SALT or @ref BLEAM_SERVICE_CLIENT_EVT_RECV_TIME. */
} bleam_service_client_evt_t;

/**@brief BLEAM Service event handler type. */
typedef void (*bleam_service_client_evt_handler_t) (bleam_service_client_t * p_bas, bleam_service_client_evt_t * p_evt);

/**@brief BLEAM Service Client initialization structure. */
typedef struct
{
    bleam_service_client_evt_handler_t evt_handler;  /**< Event handler to be called by the BLEAM Service Client module whenever there is an event related to the BLEAM Service. */
} bleam_service_client_init_t;

struct bleam_service_client_s {
    uint16_t conn_handle;                           /**< Connection handle as provided by the SoftDevice. */
    bleam_service_db_t handles;                     /**< Handles related to BLEAM Service on the peer*/
    bleam_service_client_evt_handler_t evt_handler; /**< Application event handler to be called when there is an event related to the BLEAM service. */
    uint8_t uuid_type;                              /**< UUID type. */
};

/**@brief Function for initialising the BLEAM service
 *
 * @param[in] p_bleam_service_client            Pointer to the struct of BLEAM service.
 * @param[in] p_bleam_service_client_init       Pointer to the struct storing the BLEAM service init params.
 *
 * @retval NRF_SUCCESS if BLEAM service is init.
 * @retval NRF_ERROR_NULL if any of the parameter pointers is NULL.
 * @returns otherwise, an error code of @link_ble_db_discovery_evt_register call.
 */
uint32_t bleam_service_client_init(bleam_service_client_t *p_bleam_service_client, bleam_service_client_init_t *p_bleam_service_client_init);

/**@brief Function for changing BLEAM service base UUID in the BLE stack's table
 *
 *@details Function removes previously added BLEAM service vendor specific UUID from the
 *         BLE stack's table and addn a new one, in order for DB discovery to find
 *         the service on the new BLEAM device
 *
 * @param[in] p_bleam_service_client            Pointer to the struct of BLEAM service.
 * @param[in] bleam_service_base_uuid           Pointer to the struct storing the BLEAM base UUID.
 *
 * @return NRF_SUCCESS if BLEAM service base UUID in BLE stack table is replaced successfully.
 * @returns otherwise, a non-zero error code of @link_sd_ble_uuid_vs_remove or @link_sd_ble_uuid_vs_add calls.
 */
uint32_t bleam_service_uuid_vs_replace(bleam_service_client_t *p_bleam_service_client, ble_uuid128_t *bleam_service_base_uuid);

/**@brief Function for handling ble events for BLEAM service
 *
 * @param[in] p_ble_evt                         Pointer to event received from the BLE stack.
 * @param[in] p_context                         Pointer to the struct of BLEAM service.
 *
 * @returns Nothing.
 */
void bleam_service_client_on_ble_evt(ble_evt_t const *p_ble_evt, void *p_context);

/**@brief Function for handling DB discovery events.
 *
 * @param[in] p_bleam_service_client            Pointer to the struct of BLEAM service.
 * @param[in] p_evt                             Pointer to event received from DB discovery
 *
 * @returns Nothing.
 */
void bleam_service_on_db_disc_evt(bleam_service_client_t *p_bleam_service_client, const ble_db_discovery_evt_t *p_evt);

/**@brief Function for assigning handles to BLEAM service
 *
 * @details Function assigns new connection handle for the service and peer handles for characteristics.
 *
 * @param[in] p_bleam_service_client            Pointer to the struct of BLEAM service.
 * @param[in] conn_handle                       Pointer to the struct storing conn handle.
 * @param[in] p_peer_handles                    Pointer to the struct storing peer handles.
 *
 * @retval NRF_SUCCESS if handlers are assigned successfully.
 * @retval NRF_ERROR_NULL if any of the parameter pointers is NULL.
 */
uint32_t bleam_service_client_handles_assign(bleam_service_client_t *p_bleam_service_client,
    uint16_t conn_handle,
    const bleam_service_db_t *p_peer_handles);

/**@brief Function for requesting the peer to start sending notification of NOTIFY characteristic.
 *
 * @param[in]   p_bleam_service_client       Pointer to the struct of BLEAM service.
 *
 * @retval NRF_SUCCESS if handlers are assigned successfully.
 * @retval NRF_ERROR_NULL if any of the parameter pointers is NULL.
 * @retval NRF_ERROR_INVALID_STATE if either the connection state or NOTIFY characteristic handle is invalid.
 * @returns otherwise, the return value of @link_sd_ble_gattc_write call.
 */
uint32_t bleam_service_client_notify_enable(bleam_service_client_t *p_bleam_service_client);

/**@brief Function for reading the peer TIME characteristic.
 *
 * @param[in]   p_bleam_service_client       Pointer to the struct of BLEAM service.
 *
 * @retval NRF_SUCCESS if time is read successfully.
 * @retval NRF_ERROR_NULL if any of the parameter pointers is NULL.
 * @retval NRF_ERROR_INVALID_STATE if the connection state is invalid.
 * @retval NRF_ERROR_NOT_FOUND if the TIME characteristic is not fount at the peer.
 * @returns otherwise, an error code of @link_sd_ble_gattc_read call.
 */
uint32_t bleam_service_client_read_time(bleam_service_client_t *p_bleam_service_client);

/**@brief Function for writing data to the BLEAM service
 *
 * @param[in] p_bleam_service_client            Pointer to the struct of BLEAM service.
 * @param[in] data_array                        Pointer to the data buffer.
 * @param[in] data_size                         Pointer to the data buffer length.
 * @param[in] write_handle                      Characteristic to write data to @ref bleam_service_char_t.
 *
 * @retval NRF_SUCCESS if data is written successfully.
 * @retval NRF_ERROR_NULL if any of the parameter pointers is NULL.
 * @retval NRF_ERROR_INVALID_PARAM if provided data size is invalid.
 * @retval NRF_ERROR_INVALID_STATE if the connection state is invalid.
 * @returns otherwise, an error code of @link_sd_ble_gattc_write call.
 */
uint32_t bleam_service_data_send(bleam_service_client_t *p_bleam_service_client, uint8_t *data_array, uint16_t *data_size, uint16_t write_handle);

/**@brief Function for getting current BLEAM service mode.
 *
 * @returns BLEAM service current mode value.
 */
bleam_service_client_mode_type_t bleam_service_mode_get(void);

/**@brief Function for setting a new BLEAM service mode.
 *
 * @param[in]   p_mode       Value the BLEAM service mode is to be set.
 *
 * @returns Nothing.
 */
void bleam_service_mode_set(bleam_service_client_mode_type_t p_mode);

#endif // BLEAM_SERVICE_H__

/** @}*/