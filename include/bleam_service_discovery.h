/**
 * @addtogroup bleam_discovery
 * @{
 */

#ifndef BLEAM_SERVICE_DISCOVERY_H__
#define BLEAM_SERVICE_DISCOVERY_H__

#include <stdint.h>
#include <stdbool.h>
#include "nrf_error.h"
#include "ble.h"
#include "ble_gattc.h"
#include "ble_gatt_db.h"

#include "ble_db_discovery.h"

/**@brief Macro for defining a custom service discovery instance.
 *
 * @param _name Name of the instance.
 * @hideinitializer
 */
#define BLEAM_SERVICE_DISCOVERY_DEF(_name)                       \
BLE_DB_DISCOVERY_DEF(_name);                                     \
NRF_SDH_BLE_OBSERVER(_name ## _obs_,                             \
                     BLE_DB_DISC_BLE_OBSERVER_PRIO,              \
                     bleam_service_discovery_on_ble_evt, &_name)

/**@brief Custom service discovery event type. */
typedef enum
{
    BLEAM_SERVICE_DISCOVERY_COMPLETE,      /**< Event indicating that the discovery of one service is complete. */
    BLEAM_SERVICE_DISCOVERY_ERROR,         /**< Event indicating that an internal error has occurred in the DB Discovery module. This could typically be because of the SoftDevice API returning an error code during the DB discover.*/
    BLEAM_SERVICE_DISCOVERY_SRV_NOT_FOUND, /**< Event indicating that the service was not found at the peer.*/
    BLEAM_SERVICE_DISCOVERY_AVAILABLE      /**< Event indicating that the DB discovery instance is available.*/
} bleam_service_discovery_evt_type_t;

/** @brief Structure containing the event from the DB discovery module to the application. */
typedef struct
{
    bleam_service_discovery_evt_type_t evt_type; /**< Type of event. */
    uint16_t conn_handle;                        /**< Handle of the connection for which this event has occurred. */
    union
    {
        ble_uuid128_t srv_uuid128; /**< Structure containing the 128-bit service UUID to be discovered. This will be filled when the event type is @link_ble_db_disc_complete. */
        ble_uuid_t srv_uuid16;     /**< Structure containing the 16-bit service UUID to be discovered. This will be filled when the event type is @link_ble_db_disc_complete. */
        uint32_t err_code;         /**< nRF Error code indicating the type of error which occurred in the DB Discovery module. This will be filled when the event type is @link_ble_db_disc_error. */
    } params; /**< Event parameters. */
} bleam_service_discovery_evt_t;

/**@brief Custom service discovery event handler type.
 *
 * @param[in] p_evt    Pointer to the event structure
 *
 * @returns Nothing.
 */
typedef void (* bleam_service_discovery_evt_handler_t)(const bleam_service_discovery_evt_t * p_evt);

/**@brief Function for initialising parameters for custom service discovery.
 *
 * @param[in] evt_handler       Pointer to the function that will hadnle service discovery events.
 * @param[in] p_uuid_to_find    16-bit UUID of the service to be found.
 *
 *@retval NRF_SUCCESS
 */
 uint32_t bleam_service_discovery_init(const bleam_service_discovery_evt_handler_t evt_handler, uint16_t p_uuid_to_find);

/**@brief Function for starting custom service discovery.
 *
 * @param[in] p_db_discovery    Pointer to the BLEAM sservice discovery structure.
 * @param[in] conn_handle       Connection handle.
 *
 * @returns Nothing.
 */
void bleam_service_discovery_start(ble_db_discovery_t *const p_db_discovery, uint16_t conn_handle);

/**@brief Function for handling the Application's BLE Stack events.
 *
 * @param[in]     p_ble_evt Pointer to the BLE event received.
 * @param[in,out] p_context Pointer to the BLEAM sservice Discovery structure.
 *
 * @returns Nothing.
 */
void bleam_service_discovery_on_ble_evt(ble_evt_t const * p_ble_evt, void * p_context);


#endif // BLEAM_SERVICE_DISCOVERY_H__

/** @}*/