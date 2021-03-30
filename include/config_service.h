/**
 * @addtogroup config_service
 * @{
 */

#ifndef CONFIG_S_H__
#define CONFIG_S_H__

#include <stdint.h>
#include <stdbool.h>
#include "ble.h"
#include "ble_srv_common.h"
#include "nrf_sdh_ble.h"
#include "global_app_config.h"
#include "nrf_crypto_rng.h"

/**@brief Configuration server instance structure. */
typedef struct config_s_server_s config_s_server_t;

#define CONFIG_S_UUID       APP_CONFIG_CONFIG_SERVICE_UUID         /**< UUID of provisioning device. */
#define CONFIG_S_BASE_UUID {0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, \
                            0x00, 0x00, \
                            0x00, 0x00, \
                            0x00, 0x00, \
                            (uint8_t)(0xFF & CONFIG_S_UUID), (uint8_t)(CONFIG_S_UUID >> 8), \
                            0x00, 0x00}                            /**< Base UUID for Configuration Service. */
#define CONFIG_S_CHARS_NUM  8                                      /**< Number of Configuration Service characteristics */

/**@brief Configuration Service characterisctic IDs. */
typedef enum {
    CONFIG_S_VERSION                 = 1, /**< Version characteristic. [READ] */
    CONFIG_S_STATUS,                      /**< Status characteristic. [READ WRITE NOTIFY] */
    CONFIG_S_APPKEY,                      /**< Appkey characteristic. [WRITE] */
    CONFIG_S_NODE_ID,                     /**< Node address characteristic. [WRITE] */
} config_s_char_t;

/**@brief Configuration Service status type. */
typedef enum {
    CONFIG_S_STATUS_WAITING,              /**< Waiting for appkey and node ID from API */
    CONFIG_S_STATUS_SET,                  /**< BLEAM Scanner appkey and node ID are set */
    CONFIG_S_STATUS_DONE,                 /**< BLEAM Scanner node configured and saved to server */
    CONFIG_S_STATUS_FAIL,                 /**< Configuring node failed for some reason */
} config_s_status_t;

/**@brief Configuration Service event type. */
typedef enum {
    CONFIG_S_SERVER_EVT_CONNECTED    = 0, /**< Event indicating that the Configuration Service has been discovered at the peer. */
    CONFIG_S_SERVER_EVT_DISCONNECTED = 1, /**< Event indicating that the Configuration Service has been disconnected from the peer. */
    CONFIG_S_SERVER_EVT_WRITE        = 2, /**< Event indicating that the Configuration Service has been written to. */
} config_s_server_evt_type_t;

#define CONFIG_S_SERVER_BLE_OBSERVER_PRIO 3 /**< Priority of the observer event handler for confuguration service. */
#define CONFIG_S_SERVER_DEF(_name)         \
    static config_s_server_t _name;        \
    NRF_SDH_BLE_OBSERVER(_name##_obs,          \
        CONFIG_S_SERVER_BLE_OBSERVER_PRIO, \
        config_s_server_on_ble_evt, &_name) /**< Macro for configuration service definition and registering observer. */


/**@brief Configuration Event structure. */
typedef struct {
    config_s_server_evt_type_t      evt_type;       /**< Type of the event. */
    uint16_t                      conn_handle;      /**< Connection handle on which the event occured.*/
    ble_gatts_evt_write_t const * write_evt_params; /**< Write event params. */
} config_s_server_evt_t;

/**@brief Configuration Service event handler type. */
typedef void (*config_s_server_evt_handler_t) (config_s_server_t *p_config_s_server, config_s_server_evt_t const *p_evt);

/**@brief Configuration Service server initialization structure. */
typedef struct
{
    config_s_server_evt_handler_t  evt_handler;  /**< Event handler to be called by the Configuration Service server module whenever there is an event related to the Configuration Service. */
    ble_srv_cccd_security_mode_t char_attr_md; /**< Initial security level for Custom characteristics attribute */
} config_s_server_init_t;

struct config_s_server_s {
    uint16_t                    conn_handle;                    /**< Connection handle as provided by the SoftDevice. */
    uint16_t                    service_handle;                 /**< Handle of Configuration Service (as provided by the BLE stack). */
    config_s_server_evt_handler_t evt_handler;                    /**< Application event handler to be called when there is an event related to the Configuration Service. */
    ble_gatts_char_handles_t    char_handles[CONFIG_S_CHARS_NUM]; /**< Handles of characteristics as provided by the SoftDevice. */
    uint8_t                     uuid_type;                      /**< UUID type. */
};

/**@brief Version data structure.
 *
 *@note This struct has _packed_ attribute
 */
typedef struct __attribute((packed)) {
    uint8_t  protocol_id; /**< Protocol number. */
    uint16_t fw_id;       /**< Firmware version number. */
    uint8_t  hw_id;       /**< Hardware ID. */
} config_s_server_version_t;

/**@brief Function for handling ble events for Configuration service
 *            
 *
 * @param[in]    p_ble_evt              Pointer to event received from the BLE stack.
 * @param[in]    p_context              Pointer to the struct of Configuration Service.
 *
 * @returns Nothing.
 */
void config_s_server_on_ble_evt(ble_evt_t const *p_ble_evt, void *p_context);

/**@brief Function for initialising the Configuration Service
 *
 *@details Function initialises the Configuration Service.
 *            
 *
 * @param[in]    p_config_s_server        Pointer to the struct of Configuration Service.
 * @param[in]    p_config_s_server_init   Pointer to the struct storing the Configuration Service init params.
 *
 * @retval       NRF_SUCCESS if all Configuration service characteristics have been successfully created.
 * @retval       NRF_ERROR_NULL if any of the parameter pointers is NULL.
 * @returns a bitwise OR of other errors returned by @ref config_s_char_add() calls.
 */
uint32_t config_s_server_init(config_s_server_t *p_config_s_server, config_s_server_init_t *p_config_s_server_init);

/**@brief Function for updating the status.
 *
 * @details The application calls this function when the custom value should be updated. If
 *          notification has been enabled, the custom value characteristic is sent to the client.
 *
 * @note 
 *       
 * @param[in]    p_config_s_server       Pointer to the struct of Configuration Service.
 * @param[in]    p_status                Node configuration status to be reported to BLEAM.
 *
 * @retval       NRF_SUCCESS is status is updated successfully.
 * @retval       NRF_ERROR_NULL if the parameter pointer is NULL.
 * @retval       NRF_ERROR_INVALID_STATE if connection handle is invalid.
 * @returns otherwise the return value of @link_sd_ble_gatts_hvx.
 */
uint32_t config_s_status_update(config_s_server_t *p_config_s_server, config_s_status_t p_status);

/**@brief Function for sending BLEAM Scanner version info.
 *
 * @details This function is called on successful connection with a BLEAM device for configuration.
 *          BLEAM device has to obtain BLEAM Scanner version data to pick the correct configuration data for sending back.
 *       
 * @param[in]    p_config_s_server       Pointer to the struct of Configuration Service.
 *
 * @retval       NRF_SUCCESS version info is sent successfully.
 * @retval       NRF_ERROR_NULL if the parameter pointer is NULL.
 * @retval       NRF_ERROR_INVALID_STATE if connection handle is invalid.
 * @returns otherwise the return value of @link_sd_ble_gatts_value_set.
 */
uint32_t config_s_publish_version(config_s_server_t *p_config_s_server);

/**@brief Function for getting Configuration service current status.
 *
 * @returns      Configuration service current status.
 */
config_s_status_t config_s_get_status(void);

/**@brief Function for finishing configuration process.
 *
 * @returns Nothing.
 */
void config_s_finish(void);

#endif // CONFIG_S_H__

/** @}*/