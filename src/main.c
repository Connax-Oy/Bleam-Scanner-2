/** @file main.c */

#include <stdint.h>
#include <string.h>
#include "main.h"

/************************* DECLARATIONS ***************************/

/**@addtogroup bleam_time
 * @{
 */
static uint32_t m_system_time;          /**< BLEAM Scanner system time in seconds passed since midnight */
static uint32_t m_blesc_uptime;         /**< Node uptime in minutes since last boot */
static uint32_t m_blesc_time_period;    /**< Scan period: maximum between scans */
static bool m_system_time_needs_update; /**< Flag that denoted that system time needs to be updated */
/** @} end of bleam_time */

nrf_drv_wdt_channel_id m_channel_id; /**< Watchdog timer channed ID */

static blesc_state_t m_blesc_node_state = BLESC_STATE_SCANNING;  /**< BLEAM Scanner node current state @ref blesc_state_t */

static bool m_bleam_nearby = false; /**< @ingroup bleam_scan
                                      * Flag that denotes whether a BLEAM device has been detected by BLEAM Scanner node since latest scan start  */

static void eco_timer_handler(void *p_context);

static bool m_dfu_is_init; /**< @ingroup blesc_dfu
                             * Flag denoting whether DFU mode is accessible. */
/**@addtogroup bleam_security
 * @{
 */
static uint8_t m_bleam_signature[NRF_CRYPTO_HASH_SIZE_SHA256]; /**< Array to store signature received from BLEAM. */
static uint8_t m_digest[NRF_CRYPTO_HASH_SIZE_SHA256];          /**< Array to store generated signature. */
static uint8_t m_bleam_signature_halves;                       /**< Bitwise flags denoting whether each half of the BLEAM signature have been received (binary 01 for first, 10 for second, 11 for both). */
static nrf_crypto_hmac_context_t m_context;                    /**< Context for signing */
/** @} end of bleam_security */

static uint8_t m_adv_handle = BLE_GAP_ADV_SET_HANDLE_NOT_SET; /**< Advertising handle used to identify an advertising set. */
static uint8_t m_enc_advdata[BLE_GAP_ADV_SET_DATA_SIZE_MAX];  /**< Buffer for storing an encoded advertising set. */

/** Struct that contains pointers to the encoded advertising data. */
static ble_gap_adv_data_t m_adv_data = {
    .adv_data = {
        .p_data = m_enc_advdata,
        .len = BLE_GAP_ADV_SET_DATA_SIZE_MAX
    }
};

NRF_BLE_GATT_DEF(m_gatt);                                /**< GATT module instance. */
NRF_BLE_QWR_DEF(m_qwr);                                  /**< Context for the Queued Write module.*/
static uint16_t m_conn_handle = BLE_CONN_HANDLE_INVALID; /**< Handle of the current connection. */
BLEAM_SERVICE_DISCOVERY_DEF(m_db_disc);                  /**< BLEAM discovery module instance. */
BLEAM_SERVICE_CLIENT_DEF(m_bleam_service_client);        /**< BLEAM service client instance. */
CONFIG_S_SERVER_DEF(m_config_service_server);            /**< Configuration service server instance. */

/**@addtogroup bleam_scan
 * @{
 */
NRF_BLE_SCAN_DEF(m_scan); /**< Scanning module instance. */

/** UUID of BLEAM service, first and second bytes separately */
const uint16_t uuid_bleam_to_scan[] = {BLEAM_SERVICE_UUID >> 8, BLEAM_SERVICE_UUID & 0x00FF};
/** Scanning parameters */
static ble_gap_scan_params_t m_scan_params = {
    .active        = 1,
    .interval      = SCAN_INTERVAL,
    .window        = SCAN_WINDOW,
    .timeout       = SCAN_DURATION,
    .filter_policy = BLE_GAP_SCAN_FP_ACCEPT_ALL,
    .scan_phys     = BLE_GAP_PHY_1MBPS,
};
/** @} end of bleam_scan */

APP_TIMER_DEF(m_system_time_timer_id);      /**< @ingroup bleam_time
                                              *  Timer for updating system time. */
APP_TIMER_DEF(scan_connect_timer);          /**< @ingroup bleam_connect
                                              *  Timer for scan/connect cycle. */
APP_TIMER_DEF(drop_blacklist_timer_id);     /**< @ingroup ios_solution
                                              * Battery measurement timer. */
APP_TIMER_DEF(m_eco_timer_id);              /**< @ingroup blesc_app
                                              * BLEAM Scanner sleep/wake cycle timer. */
APP_TIMER_DEF(m_eco_watchdog_timer_id);     /**< @ingroup blesc_app
                                              * Timer for feeding watchdog guring eco sleep. */
APP_TIMER_DEF(m_bleam_inactivity_timer_id); /**< @ingroup bleam_connect
                                              * BLEAM timeout for receiving salt. */

#ifdef BOARD_RUUVITAG_B
/**@ingroup blesc_debug
 * Ruuvi SDK GPIO interrupt table for LED indication.
 */
static ruuvi_interface_gpio_interrupt_fp_t interrupt_table[RUUVI_BOARD_GPIO_NUMBER + 1] = {0};
#endif

static uint8_t m_bleam_uuid_index; /**< @ingroup bleam_connect
                                    *  Index of BLEAM device to connect to in storage */

/** \addtogroup blesc_fds
 *  @{
 */
static bool volatile m_fds_initialized;                 /**< Flag to check fds initialization. */
__ALIGN(4) static configuration_t m_blesc_config = {0}; /**< BLEAM Scanner configuration data */
static ret_code_t flash_config_write(void);
static void flash_pages_erase(void);
static void flash_config_delete(void);
/** @} end of blesc_fds */
static void leave_config_mode(void);

/** \addtogroup ios_solution
 *  @{
 */
static bleam_ios_rssi_data_t stupid_ios_data = {0};                        /**< Structure for storing data for iOS device that is being investigated to have BLEAM running int the background. */
static bleam_ios_mac_whitelist_t ios_mac_whitelist[APP_CONFIG_MAX_BLEAMS]; /**< MAC address whitelist for iOS devices. */
static bleam_ios_mac_blacklist_t ios_mac_blacklist[APP_CONFIG_MAX_BLEAMS]; /**< MAC address blacklist for iOS devices. */
/** @} end of ios_solution */

/*************************** Data Storage *****************************/

/**@addtogroup bleam_storage
 * @{
 */

/** Detected devices' RSSI data storage array */
blesc_model_rssi_data_t bleam_rssi_data[APP_CONFIG_MAX_BLEAMS];

/** Function for clearing all RSSI data for a BLEAM device
 *
 * @param[in] data    Pointer to RSSI scan data entry to be cleared.
 * @returns Nothing.
*/
static void clear_rssi_data(blesc_model_rssi_data_t *data) {
    data->active = 0;
    data->scans_stored_cnt = 0;
    data->timestamp = 0;
    memset(data->bleam_uuid, 0, APP_CONFIG_BLEAM_UUID_SIZE);
    memset(data->mac, 0, BLE_GAP_ADDR_LEN);
    memset(data->rssi, INT8_MIN, APP_CONFIG_RSSI_PER_MSG);
    memset(data->aoa, 0, APP_CONFIG_RSSI_PER_MSG);
}

/** Wrapper function for calculating time difference between a timestamp and current moment.
 *
 * @param[in] past_timestamp    Value of a past timestamp.
*/
static uint32_t how_long_ago(uint32_t past_timestamp) {
    return app_timer_cnt_diff_compute(app_timer_cnt_get(), past_timestamp);
}

/**@brief Function for adding RSSI scan data to storage
 *
 * @param[in] uuid_storage_index    BLEAM device index in data storage.
 * @param[in] rssi                  Pointer to RSSI level for scanned BLEAM
 * @param[in] aoa                   Pointer to AOA data for scanned BLEAM
 *
 *@returns Boolean value denoting if RSSI data is ready to send to BLEAM now
 */
static bool app_blesc_save_rssi_to_storage(const uint8_t uuid_storage_index, const uint8_t *rssi, const uint8_t *aoa) {
    VERIFY_PARAM_NOT_NULL(rssi);
    VERIFY_PARAM_NOT_NULL(aoa);
    if (bleam_rssi_data[uuid_storage_index].scans_stored_cnt >= APP_CONFIG_RSSI_PER_MSG) {
        return true;
    }

//    if(RSSI_FILTER_TIMEOUT > how_long_ago(bleam_rssi_data[uuid_storage_index].timestamp) &&
//                        0 != how_long_ago(bleam_rssi_data[uuid_storage_index].timestamp)) {
//        return false;
//    }

    bleam_rssi_data[uuid_storage_index].rssi[bleam_rssi_data[uuid_storage_index].scans_stored_cnt] = *rssi;
    bleam_rssi_data[uuid_storage_index].aoa[bleam_rssi_data[uuid_storage_index].scans_stored_cnt] = *aoa;
    bleam_rssi_data[uuid_storage_index].timestamp = app_timer_cnt_get();
    ++bleam_rssi_data[uuid_storage_index].scans_stored_cnt;

    if (APP_CONFIG_RSSI_PER_MSG == bleam_rssi_data[uuid_storage_index].scans_stored_cnt)
        return true;
    else
        return false;
}

/**@brief Function for adding a new BLEAM device to storage
 *
 * @param[in] p_uuid    UUID of BLEAM device to add.
 * @param[in] p_mac     MAC address of BLEAM device to add.
 *
 *@returns Index of BLEAM device in storage.
 */
static uint8_t app_blesc_save_bleam_to_storage(const uint8_t * p_uuid, const uint8_t * p_mac) {
    uint8_t uuid_storage_empty_index = APP_CONFIG_MAX_BLEAMS;
    
    /* Find if received MAC has been scanned/received previously. 
    *  If it wasn't, add new bleam_rssi_data[uuid_storage_empty_index] element */
    for (uint8_t uuid_storage_index = 0; APP_CONFIG_MAX_BLEAMS > uuid_storage_index; ++uuid_storage_index) {
        /* If UUIDs match, it means it was scanned/received before and there is bleam_rssi_data[] element for it */
        if (0 == memcmp(p_uuid, bleam_rssi_data[uuid_storage_index].bleam_uuid, APP_CONFIG_BLEAM_UUID_SIZE)) {
            /* If this MAC was already saved, nothing to do here anymore */
            if (0 == memcmp(p_mac, bleam_rssi_data[uuid_storage_index].mac, BLE_GAP_ADDR_LEN)) {
                return uuid_storage_index;
            }
            /* Save MAC address. */
            for(uint8_t i = 0; BLE_GAP_ADDR_LEN > i; ++i)
                bleam_rssi_data[uuid_storage_index].mac[i] = p_mac[i];
            bleam_rssi_data[uuid_storage_index].active = 1;
            return uuid_storage_index;
        }
        if (0 == bleam_rssi_data[uuid_storage_index].active) {
            if (uuid_storage_empty_index > uuid_storage_index)
                uuid_storage_empty_index = uuid_storage_index;
            continue;
        }
    }

    if (APP_CONFIG_MAX_BLEAMS == uuid_storage_empty_index) {
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "STORAGE: Storage is full, can't save new UUID and MAC\n");
        return APP_CONFIG_MAX_BLEAMS;
    }

    /* Save UUID and MAC address */
    for (uint8_t i = 0; APP_CONFIG_BLEAM_UUID_SIZE > i; ++i)
        bleam_rssi_data[uuid_storage_empty_index].bleam_uuid[i] = p_uuid[i];
    for (uint8_t i = 0; BLE_GAP_ADDR_LEN > i; ++i)
        bleam_rssi_data[uuid_storage_empty_index].mac[i] = p_mac[i];
    bleam_rssi_data[uuid_storage_empty_index].active = 1;

    return uuid_storage_empty_index;
}

/** @} end of bleam_storage */

/** @addtogroup ios_solution
 *  @{
 */

/**@brief Function for searching for a MAC address in iOS whitelist
 *
 * @param[in] p_mac     Pointer to MAC address to look for.
 * @param[in] p_uuid    Pointer to the UUID to look for.
 *
 *@returns In case the address with the UUID is present in whitelist,
 *         returns the pointer to corresponding UUID, otherwise returns a NULL pointer.
 */
static uint8_t * mac_in_whitelist(const uint8_t * p_mac, uint8_t * p_uuid) {
    uint8_t * res = NULL;
    for(uint8_t index = 0; APP_CONFIG_MAX_BLEAMS > index; ++index) {
        if(!ios_mac_whitelist[index].active)
            continue;
        if(0 == memcmp(ios_mac_whitelist[index].mac, p_mac, BLE_GAP_ADDR_LEN)) {
            res = ios_mac_whitelist[index].bleam_uuid;
            ios_mac_whitelist[index].timestamp = app_timer_cnt_get();
            // If UUID is known and has changed, update it
            if(p_uuid != NULL && 0 != memcmp(ios_mac_whitelist[index].bleam_uuid, p_uuid, APP_CONFIG_BLEAM_UUID_SIZE)) {
                memcpy(ios_mac_whitelist[index].bleam_uuid, p_uuid, APP_CONFIG_BLEAM_UUID_SIZE);
            }
        } else if(MACLIST_TIMEOUT < how_long_ago(ios_mac_whitelist[index].timestamp)) {
            ios_mac_whitelist[index].active = false;
        }
    }
    return res;
}

/**@brief Function for adding a MAC address to iOS whitelist
 *
 * @param[in] p_mac     Pointer to MAC address of BLEAM device to add.
 * @param[in] p_uuid    Pointer to UUID of BLEAM device to add.
 *
 *@retval NRF_SUCCESS if device is successfully whitelisted.
 *@retval NRF_ERROR_NO_MEM if whitelist is full.
 */
static ret_code_t add_mac_in_whitelist(const uint8_t * p_mac, uint8_t * p_uuid) {
    for(uint8_t index = 0; APP_CONFIG_MAX_BLEAMS > index; ++index) {
        if(ios_mac_whitelist[index].active)
            continue;
        ios_mac_whitelist[index].active = true;
        memcpy(ios_mac_whitelist[index].mac, p_mac, BLE_GAP_ADDR_LEN);
        memcpy(ios_mac_whitelist[index].bleam_uuid, p_uuid, APP_CONFIG_BLEAM_UUID_SIZE);
        ios_mac_whitelist[index].timestamp = app_timer_cnt_get();
        return NRF_SUCCESS;
    }
    return NRF_ERROR_NO_MEM;
}

/**@brief Function for searching for a MAC address in iOS blacklist
 *
 * @param[in] p_mac    Pointer to MAC address to look for.
 *
 *@retval true in case the address is blacklisted.
 *@retval false otherwise.
 */
static bool mac_in_blacklist(const uint8_t * p_mac) {
    bool res = false;
    for(uint8_t index = 0; APP_CONFIG_MAX_BLEAMS > index; ++index) {
        if(!ios_mac_blacklist[index].active)
            continue;
        if(0 == memcmp(ios_mac_blacklist[index].mac, p_mac, BLE_GAP_ADDR_LEN)) {
            res = true;
            ios_mac_blacklist[index].timestamp = app_timer_cnt_get();
        } else if(MACLIST_TIMEOUT < how_long_ago(ios_mac_blacklist[index].timestamp))
            ios_mac_blacklist[index].active = false;
    }
    return res;
}

/**@brief Function for adding a MAC address to iOS blacklist
 *
 *
 * @param[in] p_mac     Pointer to MAC address of BLEAM device to add.
 *
 *@retval NRF_SUCCESS if device is successfully blacklisted.
 *@retval NRF_ERROR_NO_MEM if blacklist is full.
 */
static ret_code_t add_mac_in_blacklist(const uint8_t * p_mac) {
    for(uint8_t index = 0; APP_CONFIG_MAX_BLEAMS > index; ++index) {
        if(ios_mac_blacklist[index].active)
            continue;
        ios_mac_blacklist[index].active = true;
        memcpy(ios_mac_blacklist[index].mac, p_mac, BLE_GAP_ADDR_LEN);
        ios_mac_blacklist[index].timestamp = app_timer_cnt_get();
        return NRF_SUCCESS;
    }
    return NRF_ERROR_NO_MEM;
}

/**@brief Function for handlind timeout event for drop_blacklist_timer_id.
 *@details Function erases all MAC addresses from iOS blacklist on timeout.
 *
 *
 * @param[in] p_context   Pointer used for passing some arbitrary information (context) from the
 *                        app_start_timer() call to the timeout handler.
 *
 * @returns Nothing.
 */
static void drop_blacklist(void * p_context) {
    memset(ios_mac_blacklist, 0, APP_CONFIG_MAX_BLEAMS * sizeof(bleam_ios_mac_blacklist_t));
}

/** @} end of ios_solution */

/**********************  INTERNAL FUNCTIONS  ************************/

/**@brief Function to update system time value.
 * @ingroup bleam_time
 *
 * @param[in]  bleam_time   New system time value.
 *
 * @returns Nothing.
 */
static void system_time_update(uint32_t * bleam_time) {
    m_system_time = (*bleam_time) / 1000;
    m_system_time_needs_update = false;
}

/**@brief Function for controlling LEDs on BLEAM Scanner.
 * @ingroup blesc_debug
 *
 * @param[in] scanning_led_state 	State to be of the LED indicating scanning action.
 * @param[in] connected_led_state 	State to be of the LED indicating connection action.
 *
 * @returns Nothing.
 */
static void blesc_toggle_leds(bool scanning_led_state, bool connected_led_state) {
#if NRF_MODULE_ENABLED(DEBUG)
  #ifndef BOARD_RUUVITAG_B
    if(scanning_led_state)
        bsp_board_led_on(CENTRAL_SCANNING_LED);
    else
        bsp_board_led_off(CENTRAL_SCANNING_LED);
    if(connected_led_state)
        bsp_board_led_on(CENTRAL_CONNECTED_LED);
    else
        bsp_board_led_off(CENTRAL_CONNECTED_LED);
  #else
    ruuvi_interface_gpio_id_t pin;
    pin.pin = CENTRAL_SCANNING_LED;
    ruuvi_interface_gpio_write(pin, scanning_led_state ? RUUVI_INTERFACE_GPIO_LOW : RUUVI_INTERFACE_GPIO_HIGH);
    pin.pin = CENTRAL_CONNECTED_LED;
    ruuvi_interface_gpio_write(pin, connected_led_state ? RUUVI_INTERFACE_GPIO_LOW : RUUVI_INTERFACE_GPIO_HIGH);
    // IDK why I have to switch HIGH and LOW
  #endif
#endif
}

#ifdef BLESC_DFU
/**@brief Handler for shutdown preparation.
 * @ingroup blesc_dfu
 *
 * @details During shutdown procedures, this function will be called at a 1 second interval
 *          untill the function returns true. When the function returns true, it means that the
 *          app is ready to reset to DFU mode.
 *
 *          This function returns true all the time, but you can handle unfinished business
 *          befure shutdown in here if you need.
 *
 * @param[in]   event   Power manager event.
 *
 * @retval true if shutdown is allowed by this power manager handler.
 * @retval false otherwise.
 */
static bool app_shutdown_handler(nrf_pwr_mgmt_evt_t event)
{
    switch (event)
    {
        case NRF_PWR_MGMT_EVT_PREPARE_DFU:
            NRF_LOG_INFO("Power management wants to reset to DFU mode.");
            // YOUR_JOB: Get ready to reset into DFU mode
            //
            // If you aren't finished with any ongoing tasks, return "false" to
            // signal to the system that reset is impossible at this stage.
            //
            // Here is an example using a variable to delay resetting the device.
            //
            // if (!m_ready_for_reset)
            // {
            //      return false;
            // }
            // else
            //{
            //
            //    // Device ready to enter
            //    uint32_t err_code;
            //    err_code = sd_softdevice_disable();
            //    APP_ERROR_CHECK(err_code);
            //    err_code = app_timer_stop_all();
            //    APP_ERROR_CHECK(err_code);
            //}
            break;

        default:
            return true;
    }

    NRF_LOG_INFO("Power management allowed to reset to DFU mode.");
    return true;
}

/* Register application shutdown handler with priority 0. */
NRF_PWR_MGMT_HANDLER_REGISTER(app_shutdown_handler, 0);

/**@brief Function for booting into the bootloader.
 * @ingroup blesc_dfu
 *
 * @retval NRF_SUCCESS in case of successful shutdown.
 */
static ret_code_t enter_dfu_mode(void) {
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "====== Entering DFU ======\n");
    blesc_toggle_leds(0, 0);
    ret_code_t err_code;

    err_code = sd_power_gpregret_clr(0, 0xffffffff);
    VERIFY_SUCCESS(err_code);

    err_code = sd_power_gpregret_set(0, BOOTLOADER_DFU_START);
    VERIFY_SUCCESS(err_code);

    // Signal that DFU mode is to be enter to the power management module
    nrf_pwr_mgmt_shutdown(NRF_PWR_MGMT_SHUTDOWN_GOTO_DFU);

    return NRF_SUCCESS;
}
#endif

/**@brief Function for assert macro callback.
 *
 * @details This function will be called in case of an assert in the SoftDevice.
 *
 * @warning This handler is an example only and does not fit a final product. You need to analyse
 *          how your product is supposed to react in case of Assert.
 * @warning On assert from the SoftDevice, the system can only recover on reset.
 *
 * @param[in] line_num    Line number of the failing ASSERT call.
 * @param[in] p_file_name File name of the failing ASSERT call.
 *
 * @returns Nothing.
 */
void assert_nrf_callback(uint16_t line_num, const uint8_t *p_file_name) {
    app_error_handler(DEAD_BEEF, line_num, p_file_name);
}

/**@brief Function for decoding the BLE address type.
 * @ingroup bleam_scan
 *
 * @param[in] p_addr 	The BLE address.
 *
 * @returns @link_ble_gap_addr_types or an error value BLE_ERROR_GAP_INVALID_BLE_ADDR (see @link_ble_gap_errors).
 */
static uint16_t scan_address_type_decode(uint8_t const *p_addr) {
    uint8_t addr_type = p_addr[0];

    // See Bluetooth Core Specification Vol 6, Part B, section 1.3.
    addr_type = addr_type >> 6;
    addr_type &= 0x03;

    switch (addr_type) {
    case 0:
        return BLE_GAP_ADDR_TYPE_RANDOM_PRIVATE_NON_RESOLVABLE;
    case 1:
        return BLE_GAP_ADDR_TYPE_PUBLIC;
    case 2:
        return BLE_GAP_ADDR_TYPE_RANDOM_PRIVATE_RESOLVABLE;
    case 3:
        return BLE_GAP_ADDR_TYPE_RANDOM_STATIC;
    default:
        return BLE_ERROR_GAP_INVALID_BLE_ADDR;
    }
}

/**@brief Function to start scanning.
 * @ingroup bleam_scan
 *
 * @returns Nothing.
 */
static void scan_start(void) {
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "BLE scanner started\r\n");

    ret_code_t err_code;

    m_blesc_node_state = BLESC_STATE_SCANNING;
    m_bleam_nearby = false;

    err_code = app_timer_start(scan_connect_timer, SCAN_CONNECT_TIME, NULL);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_ble_scan_start(&m_scan);
    APP_ERROR_CHECK(err_code);

    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Scanning for UUID %04X\r\n", BLEAM_SERVICE_UUID);

    blesc_toggle_leds(1, 0);
}

/**@brief Function to stop scanning.
 * @ingroup bleam_scan
 *
 * @returns Nothing.
 */
static void scan_stop(void) {
    nrf_ble_scan_stop();
    app_timer_stop(scan_connect_timer);

    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Scanning stopped\r\n");
    blesc_toggle_leds(0, 0);
}

/**@brief Function for starting advertising.
 * @ingroup blesc_config
 *
 * @returns Nothing.
 */
static void advertising_start(void) {
    ret_code_t err_code = sd_ble_gap_adv_start(m_adv_handle, APP_BLE_CONN_CFG_TAG);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for creating signature digest with HMAX SHA256 and given key.
 * @ingroup bleam_security
 *
 * @param[out] p_digest    Pointer to array to store the hashing result in.
 * @param[in]  data        Pointer to array with data to hash.
 * @param[in]  sign_key    Pointer to array with key to hash with.
 *
 * @returns Nothing.
 */
static void sign_data(uint8_t *p_digest, uint8_t *data, uint8_t *sign_key) {
    char hex_buff[HEX_MAX_BUF_SIZE];
	
    uint32_t err_code = NRF_SUCCESS;

    size_t digest_len = NRF_CRYPTO_HASH_SIZE_SHA256;
    memset(p_digest, 0, digest_len);
      
    // Initialize the hash context
    err_code = nrf_crypto_hmac_init(&m_context, 
                                    &g_nrf_crypto_hmac_sha256_info,
                                    sign_key,                             // Pointer to key buffer.
                                    BLEAM_KEY_SIZE);                   // Length of key.
 
    APP_ERROR_CHECK(err_code);

    // Run the update function (this can be run multiples of time if the data is accessible
    // in smaller chunks, e.g. when received on-air.
    err_code = nrf_crypto_hmac_update(&m_context, data, SALT_SIZE);
    APP_ERROR_CHECK(err_code);

    //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Source string (length %u:)", p_for_sign.length)
    //NRF_LOG_HEXDUMP_INFO( p_for_sign.p_data, sizeof(p_for_sign.p_data) );

    // Run the finalize when all data has been fed to the update function.
    // this gives you the result
    err_code = nrf_crypto_hmac_finalize(&m_context, p_digest, &digest_len);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for preparing and executing a connection to BLEAM.
 * @ingroup bleam_connect
 *
 * @param[out] p_mac       MAC address of a BLEAM device to connect to.
 *
 * @returns Nothing.
 */
static void try_connect(uint8_t * p_mac) {
    ASSERT(NULL != p_mac);

    // If connection is happening right now, we can't connect
    if (BLE_CONN_HANDLE_INVALID != m_conn_handle)
        return;

    m_blesc_node_state = BLESC_STATE_CONNECT;

    ble_gap_addr_t p_ble_gap_addr = {
        .addr_type = scan_address_type_decode(p_mac),
    };
    for (uint8_t i = 0; i < BLE_GAP_ADDR_LEN; ++i) {
        p_ble_gap_addr.addr[i] = p_mac[i];
    }

    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "MAC: %02x-%02x-%02x-%02x-%02x-%02x\r\n",
        p_ble_gap_addr.addr[5],
        p_ble_gap_addr.addr[4],
        p_ble_gap_addr.addr[3],
        p_ble_gap_addr.addr[2],
        p_ble_gap_addr.addr[1],
        p_ble_gap_addr.addr[0]);
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Addr type: 0x%02X\r\n", p_ble_gap_addr.addr_type);

    if (BLE_GAP_ADDR_TYPE_ANONYMOUS == p_ble_gap_addr.addr_type || BLE_ERROR_GAP_INVALID_BLE_ADDR == p_ble_gap_addr.addr_type) {
        scan_start();
        return;
    }

    // doesn't seem to connect well to public address
    if (BLE_GAP_ADDR_TYPE_PUBLIC == p_ble_gap_addr.addr_type) {
        p_ble_gap_addr.addr_type = BLE_GAP_ADDR_TYPE_RANDOM_STATIC;
    }

    ble_gap_scan_params_t p_scan_params;
    memcpy(&p_scan_params, &(m_scan.scan_params), sizeof(ble_gap_scan_params_t));
    p_scan_params.timeout = CONNECT_TIMEOUT;
    ble_gap_conn_params_t const *p_conn_params = &(m_scan.conn_params);
    uint8_t con_cfg_tag = m_scan.conn_cfg_tag;

    ret_code_t err_code = sd_ble_gap_connect(&p_ble_gap_addr,
                                             (ble_gap_scan_params_t const *)(&p_scan_params),
                                             p_conn_params,
                                             con_cfg_tag);
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "BLE GAP CONNECT RETURN CODE: %d\r\n", err_code);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for trying to connect to chosen BLEAM device.
 * @ingroup bleam_connect
 *
 * @param[in] p_index    Index of chosen BLEAM device data in storage.
 *
 * @returns Nothing.
 */
static void try_bleam_connect(uint8_t p_index) {
    m_bleam_uuid_index = p_index;

    __LOG_XB(LOG_SRC_APP, LOG_LEVEL_INFO, "\n\n\nConnecting to BLEAM with UUID",
        bleam_rssi_data[m_bleam_uuid_index].bleam_uuid, APP_CONFIG_BLEAM_UUID_SIZE);

    try_connect(bleam_rssi_data[m_bleam_uuid_index].mac);
}

/**@brief Function for trying to connect to chosen BLEAM device on iOS
 *        with BLEAM service running in the background.
 * @ingroup bleam_connect
 *
 * @returns Nothing.
 */
static void try_ios_connect() {
    __LOG(LOG_SRC_APP, LOG_LEVEL_DBG1, "\n\n\nSTUPID Connecting to iOS BLEAM\r\n");

    try_connect(stupid_ios_data.mac);
}

/***********************  HANDLERS  *************************/

/**@addtogroup handlers
 * @{
 */

/**@brief Function for handling the idle state (main loop).
 * @ingroup blesc_app
 *
 * @details If there is no pending log operation, then sleep until next the next event occurs.
 *
 * @returns Nothing.
 */
static void idle_state_handle(void) {
    UNUSED_RETURN_VALUE(NRF_LOG_PROCESS());
    nrf_pwr_mgmt_run();
    nrf_drv_wdt_channel_feed(m_channel_id);
}

/**@brief Function for handling the system time increments.
 * @ingroup bleam_time
 *
 * @param[in] p_context   Pointer used for passing some arbitrary information (context) from the
 *                        app_start_timer() call to the timeout handler.
 *
 * @returns Nothing.
 */
static void system_time_timer_handler(void * p_context) {
    // If in eco mode, feed watchdog as well
    if (m_blesc_node_state == BLESC_STATE_IDLE) {
        nrf_drv_wdt_channel_feed(m_channel_id);
    }
    ++m_system_time;
    if (m_system_time >= 24 * 60 * 60) {
        m_system_time = 0;
        m_system_time_needs_update = true;
    }

    if(0 == m_system_time % 60) {
        ++m_blesc_uptime;
    }

    if (BLESC_DAYTIME_START == m_system_time) {
        m_blesc_time_period = BLESC_TIME_PERIODS_DAY * BLESC_TIME_PERIOD_SECS;
    } else if (BLESC_NIGHTTIME_START == m_system_time) {
        m_blesc_time_period = BLESC_TIME_PERIODS_NIGHT * BLESC_TIME_PERIOD_SECS;
    }

    // try start scan every period
    if(0 == m_system_time % m_blesc_time_period && m_blesc_node_state == BLESC_STATE_IDLE) {
        eco_timer_handler(NULL);
    }
}

/**@brief Function for handling the scan-connect timer timeout
 * @ingroup bleam_connect
 *
 * @param[in] p_context   Pointer used for passing some arbitrary information (context) from the
 *                        app_start_timer() call to the timeout handler.
 *
 * @returns Nothing.
 */
static void scan_connect_timer_handle(void *p_context) {
    if (m_bleam_nearby == false) {
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "BLESC doesn't see any BLEAMs around.\r\n");
        for(uint8_t index = 0; APP_CONFIG_MAX_BLEAMS > index; ++index) {
            clear_rssi_data(&bleam_rssi_data[index]);
        }
        eco_timer_handler(NULL);
        return;
    }

    scan_stop();
    m_blesc_node_state = BLESC_STATE_CONNECT;

    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "BLEAM scan timed out, looking for BLEAM to connect.\r\n");

    for(uint8_t index = 0; APP_CONFIG_MAX_BLEAMS > index; ++index) {
        if(bleam_rssi_data[index].active) {
            try_bleam_connect(index);
            return;
        }
    }

    // In case there's no BLEAMS in storage, make some
    m_blesc_node_state = BLESC_STATE_SCANNING;
    scan_start();
}

/**@brief Function for handling the Eco Watchdog timer timeout.
 * @ingroup blesc_app
 *
 * @details This function will feed the watchdog while the node is in Eco mode
 *
 * @param[in] p_context   Pointer used for passing some arbitrary information (context) from the
 *                        app_start_timer() call to the timeout handler.
 *
 * @returns Nothing.
 */
static void eco_watchdog_timer_handler(void * p_context) {
    nrf_drv_wdt_channel_feed(m_channel_id);
}

/**@brief Function for handling the Eco timer timeout.
 * @ingroup blesc_app
 *
 * @details This function will be called each time the eco timer expires.
 *
 * @param[in] p_context   Pointer used for passing some arbitrary information (context) from the
 *                        app_start_timer() call to the timeout handler.
 *
 * @returns Nothing.
 */
static void eco_timer_handler(void * p_context) {
    UNUSED_PARAMETER(p_context);
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Eco timer interrupt\r\n");
    switch(m_blesc_node_state) {
    case BLESC_STATE_CONNECT:
        m_blesc_node_state = BLESC_STATE_SCANNING;
    case BLESC_STATE_IDLE:
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Eco IDLE -> SCANNING\r\n");
        m_blesc_node_state = BLESC_STATE_SCANNING;
        app_timer_start(m_eco_timer_id, BLESC_SCAN_TIME, NULL);
        app_timer_stop(m_eco_watchdog_timer_id);
        // clear old lists
        mac_in_blacklist(NULL);
        mac_in_whitelist(NULL, NULL);
        scan_start();
        break;
    case BLESC_STATE_SCANNING:
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Eco SCANNING -> IDLE\r\n");
        app_timer_start(m_eco_watchdog_timer_id, APP_TIMER_TICKS(1500), NULL);
        scan_stop();
        m_blesc_node_state = BLESC_STATE_IDLE;
        // In case BLEAM Scanner is going to idle for a long time,
        // make sure it asks for time on next connection
        m_system_time_needs_update = true;
        break;
    }
}

/**@brief Function for handling the BLEAM salt wait timeout.
 * @ingroup bleam_connect
 *
 * @details This function will be called each time the BLEAM salt wait timer expires.
 *
 * @param[in] p_context   Pointer used for passing some arbitrary information (context) from the
 *                        app_start_timer() call to the timeout handler.
 *
 * @returns Nothing.
 */
static void bleam_inactivity_timeout_handler(void *p_context) {
    if(BLE_CONN_HANDLE_INVALID != m_conn_handle) {
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Didn't receive data from BLEAM\r\n");
        clear_rssi_data(&bleam_rssi_data[m_bleam_uuid_index]);
        ret_code_t err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
        if(NRF_ERROR_INVALID_STATE != err_code)
            APP_ERROR_CHECK(err_code);
    }
}

#if NRF_MODULE_ENABLED(DEBUG)
/**@brief Function for handling events from the BSP module.
 * @ingroup blesc_debug
 *
 * @param[in]   button       Number of the pressed button.
 *
 * @returns Nothing.
 */
void button_event_handler(uint8_t button) {
    if (NULL == m_blesc_config.node_id) {
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Device isn't configured.\r\n");
#if defined(BLESC_DFU)
        enter_dfu_mode();
#else
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "This firmware does not support DFU.\r\n");
#endif
    } else {
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "====== Node unconfiguration ======\n");
        flash_config_delete();
    }
}

#ifndef BOARD_RUUVITAG_B
/**@brief Function for handling events from the BSP module.
 * @ingroup blesc_debug
 *
 * @param[in]   event   Event generated by button press.
 *
 * @returns Nothing.
 */
void bsp_event_handler(bsp_event_t event) {
    switch (event)
    {
        case BSP_EVENT_KEY_0:
        case BSP_EVENT_KEY_1:
        case BSP_EVENT_KEY_2:
        case BSP_EVENT_KEY_3:
            button_event_handler(event - BSP_EVENT_KEY_0);
            break;

        default:
            break;
    }
}
#else
/**@brief Function for handling events from the button handler module.
 * @ingroup blesc_debug
 *
 * @param[in] event         The pin that the event applies to.
 *
 * @returns Nothing.
 */
static void ruuvi_button_event_handler(ruuvi_interface_gpio_evt_t event) {
    ret_code_t err_code;

    switch (event.pin.pin) {
    case RUUVI_BOARD_BUTTON_1:
        button_event_handler(0);
        break;
    }
}
#endif
#endif

/**@brief WDT events handler.
 * @ingroup blesc_app
 *
 * @returns Nothing.
 */
void wdt_event_handler(void)
{
    //NOTE: The max amount of time we can spend in WDT interrupt is two cycles of 32768[Hz] clock - after that, reset occurs
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Watchdog interrupt!\r\n");
    blesc_toggle_leds(0, 0);
}

/**@brief Function for handling database discovery events.
 * @ingroup bleam_connect
 *
 * @details This function is a callback function to handle events from the database discovery module.
 *          Depending on the UUIDs that are discovered, this function forwards the events
 *          to their respective services.
 *
 * @param[in] p_evt    Pointer to the database discovery event.
 *
 * @returns Nothing.
 */
static void db_disc_handler(ble_db_discovery_evt_t *p_evt) {
    bleam_service_on_db_disc_evt(&m_bleam_service_client, p_evt);
}

/**@brief Function for handling the BLEAM service custom discovery events.
 * @ingroup bleam_connect
 *
 * @param[in]     p_evt    Pointer to the BLEAM service discovery event.
 *
 * @returns Nothing.
 */
void bleam_service_discovery_evt_handler(const bleam_service_discovery_evt_t *p_evt) {
    // Whatever it was, we have to disconnect
    if(p_evt->conn_handle != m_conn_handle)
        return;
    ret_code_t err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
    if(NRF_ERROR_INVALID_STATE != err_code)
        APP_ERROR_CHECK(err_code);
    // Check if the BLEAM Service was discovered on iOS.
    if (p_evt->evt_type == BLEAM_SERVICE_DISCOVERY_COMPLETE && p_evt->params.srv_uuid16.uuid == BLEAM_SERVICE_UUID) {
        if(stupid_ios_data.active) {
            m_bleam_nearby = true;

            for(int i = 1 + APP_CONFIG_BLEAM_UUID_SIZE, j = 0; i > 1;)
                stupid_ios_data.bleam_uuid[j++] = p_evt->params.srv_uuid128.uuid128[i--];
            // Save device to storage
            if(!mac_in_whitelist(stupid_ios_data.mac, stupid_ios_data.bleam_uuid))
                add_mac_in_whitelist(stupid_ios_data.mac, stupid_ios_data.bleam_uuid);
            const uint8_t uuid_index = app_blesc_save_bleam_to_storage(stupid_ios_data.bleam_uuid, stupid_ios_data.mac);
            stupid_ios_data.active = 0;
            // If storage is full
            if (APP_CONFIG_MAX_BLEAMS == uuid_index) {
                __LOG(LOG_SRC_APP, LOG_LEVEL_WARN, "BLEAM storage full!\r\n");
                return;
            }
            __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Scanned iOS RSSI %d\r\n", (int8_t)stupid_ios_data.rssi);
            uint8_t aoa = 0;
            if(app_blesc_save_rssi_to_storage(uuid_index, &stupid_ios_data.rssi, &aoa)) {
                try_bleam_connect(uuid_index);
            }
        }
    } else if (p_evt->evt_type == BLE_DB_DISCOVERY_SRV_NOT_FOUND ||
               p_evt->evt_type == BLEAM_SERVICE_DISCOVERY_SRV_NOT_FOUND ||
               p_evt->evt_type == BLEAM_SERVICE_DISCOVERY_ERROR) {
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Wrong iOS device!\r\n");
        if(!mac_in_blacklist(stupid_ios_data.mac))
            add_mac_in_blacklist(stupid_ios_data.mac);
        stupid_ios_data.active = 0;
        eco_timer_handler(NULL);
    }
}

/**@brief Function for handling BLE events.
 * @ingroup bleam_connect
 *
 * @param[in]   p_ble_evt   Bluetooth stack event.
 * @param[in]   p_context   Unused.
 *
 * @returns Nothing.
 */
static void ble_evt_handler(ble_evt_t const *p_ble_evt, void *p_context) {
    uint32_t err_code;
    ble_gap_evt_t const *p_gap_evt = &p_ble_evt->evt.gap_evt;

    switch (p_ble_evt->header.evt_id) {
    case BLE_GAP_EVT_CONNECTED:
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Gap event: Connected\r\n");

        if(BLE_CONN_HANDLE_INVALID == p_gap_evt->conn_handle) {
            __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Connection handle is bogus\r\n");
            err_code = sd_ble_gap_disconnect(p_gap_evt->conn_handle,
                BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            if(NRF_ERROR_INVALID_STATE != err_code)
                APP_ERROR_CHECK(err_code);
        }

        // Connected to config service
        if(CONFIG_S_STATUS_WAITING == config_s_get_status()) {
            sd_ble_gap_adv_stop(m_adv_handle);
            m_conn_handle = p_gap_evt->conn_handle;
            err_code = nrf_ble_qwr_conn_handle_assign(&m_qwr, m_conn_handle);
            APP_ERROR_CHECK(err_code);
        } else
        // If unknown iOS, we look for BLEAM service the stupid way via service discovery and GATTC read
        if (CONFIG_S_STATUS_DONE == config_s_get_status() && stupid_ios_data.active) {
            m_conn_handle = p_gap_evt->conn_handle;
            bleam_service_discovery_start(&m_db_disc, m_conn_handle);
            __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Discovering services on iOS.\r\n");
        } else
        // Connected to BLEAM and configuration is over
        if (CONFIG_S_STATUS_DONE == config_s_get_status() && APP_CONFIG_MAX_BLEAMS != m_bleam_uuid_index) {
            err_code = bleam_service_client_handles_assign(&m_bleam_service_client, p_gap_evt->conn_handle, NULL);
            APP_ERROR_CHECK(err_code);

            err_code = bsp_indication_set(BSP_INDICATE_CONNECTED);
            APP_ERROR_CHECK(err_code);
            m_conn_handle = p_gap_evt->conn_handle;
            err_code = nrf_ble_qwr_conn_handle_assign(&m_qwr, m_conn_handle);
            APP_ERROR_CHECK(err_code);

            ble_uuid128_t m_bleam_service_base_uuid = {BLE_UUID_BLEAM_SERVICE_BASE_UUID};
            for(uint8_t i = 1 + APP_CONFIG_BLEAM_UUID_SIZE, j = 0; APP_CONFIG_BLEAM_UUID_SIZE > j;) {
                m_bleam_service_base_uuid.uuid128[i--] = bleam_rssi_data[m_bleam_uuid_index].bleam_uuid[j++];
            }
            __LOG_XB(LOG_SRC_APP, LOG_LEVEL_INFO, "Add new BASE UUID", m_bleam_service_base_uuid.uuid128, 16);
            err_code = bleam_service_uuid_vs_replace(&m_bleam_service_client, &m_bleam_service_base_uuid);
            APP_ERROR_CHECK(err_code);

            memset(&m_db_disc, 0, sizeof(m_db_disc));
            err_code = ble_db_discovery_start(&m_db_disc, m_conn_handle);
            APP_ERROR_CHECK(err_code);
//            err_code = app_timer_start(m_bleam_inactivity_timer_id, APP_TIMER_TICKS(3000), NULL);
//            APP_ERROR_CHECK(err_code);
            __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Discovering services\r\n");

            blesc_toggle_leds(0, 1);
        } else {
            __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Connection shouldn't happen\r\n");
            err_code = sd_ble_gap_disconnect(p_gap_evt->conn_handle,
                BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            if(NRF_ERROR_INVALID_STATE != err_code)
                APP_ERROR_CHECK(err_code);
        }
        break;

    case BLE_GAP_EVT_DISCONNECTED:
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Gap event: Disconnected\r\n");
        m_conn_handle = BLE_CONN_HANDLE_INVALID;
        if(CONFIG_S_STATUS_DONE == config_s_get_status()) {
            if(1 == stupid_ios_data.active) {
                memset(&stupid_ios_data, 0, sizeof(bleam_ios_rssi_data_t));
            } else if(2 == stupid_ios_data.active) {
                try_ios_connect();
            }
            // clear old lists
            mac_in_whitelist(NULL, NULL);
            mac_in_blacklist(NULL);

            scan_start();
        }
        break;

    case BLE_GAP_EVT_TIMEOUT:
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Gap event: Timeout\r\n");
        scan_start();
        break;

    case BLE_GAP_EVT_CONN_PARAM_UPDATE_REQUEST:
        // Accept parameters requested by peer.
        err_code = sd_ble_gap_conn_param_update(p_gap_evt->conn_handle,
            &p_gap_evt->params.conn_param_update_request.conn_params);
        APP_ERROR_CHECK(err_code);
        break;

    case BLE_GAP_EVT_PHY_UPDATE_REQUEST:
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "PHY update request.\r\n");
        ble_gap_phys_t const phys = {
            .rx_phys = BLE_GAP_PHY_AUTO,
            .tx_phys = BLE_GAP_PHY_AUTO,
        };
        err_code = sd_ble_gap_phy_update(p_gap_evt->conn_handle, &phys);
        APP_ERROR_CHECK(err_code);
        break;

    case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
        // Pairing not supported
        err_code = sd_ble_gap_sec_params_reply(m_conn_handle, BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP, NULL, NULL);
        APP_ERROR_CHECK(err_code);
        break;

    case BLE_GATTS_EVT_SYS_ATTR_MISSING:
        // No system attributes have been stored.
        err_code = sd_ble_gatts_sys_attr_set(m_conn_handle, NULL, 0, 0);
        APP_ERROR_CHECK(err_code);
        break;

    case BLE_GATTC_EVT_TIMEOUT:
        // Disconnect on GATT Client timeout event.
        if(BLE_CONN_HANDLE_INVALID != m_conn_handle && p_ble_evt->evt.gattc_evt.conn_handle == m_conn_handle) {
            __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "GATT Client Timeout.\r\n");
            err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            if(NRF_ERROR_INVALID_STATE != err_code)
                APP_ERROR_CHECK(err_code);
        }
        break;

    case BLE_GATTS_EVT_TIMEOUT:
        // Disconnect on GATT Server timeout event.
        if(BLE_CONN_HANDLE_INVALID != m_conn_handle && p_ble_evt->evt.gatts_evt.conn_handle == m_conn_handle) {
            __LOG(LOG_SRC_APP, LOG_LEVEL_INFO,  "GATT Server Timeout.\r\n");
            err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            if(NRF_ERROR_INVALID_STATE != err_code)
                APP_ERROR_CHECK(err_code);
        }
        break;

    case BLE_GATTC_EVT_WRITE_CMD_TX_COMPLETE: {
        // Finished writing a package to BLEAM, may proceed
        if (CONFIG_S_STATUS_DONE == config_s_get_status()) {
            // If device is configured, we were probably sending RSSI data
            bleam_send_continue();
        } else if (CONFIG_S_STATUS_FAIL == config_s_get_status()) {
            // If device isn't configured and config status written was FAIL, reinit FDS and reset
            nrf_sdh_disable_request();
            flash_pages_erase();
            sd_nvic_SystemReset();
        }
        break;
    }

    default:
        // No implementation needed.
        break;
    }
}

/**@brief Function for handling the Configuration Service events.
 * @ingroup blesc_config
 *
 * @details This function will be called for all Configuration Service events which are passed to
 *          the application.
 *
 * @param[in]   p_config_s_server Configuration Service structure.
 * @param[in]   p_evt             Event received from the Configuration Service.
 *
 * @returns Nothing.
 */
static void config_s_event_handler(config_s_server_t *p_config_s_server, config_s_server_evt_t const *p_evt) {
    ret_code_t err_code;

    switch (p_evt->evt_type) {

    case CONFIG_S_SERVER_EVT_CONNECTED:
        __LOG(LOG_SRC_APP, LOG_LEVEL_DBG2, "Config Service: Connected\r\n");
        config_s_publish_version(p_config_s_server);
        config_s_status_update(p_config_s_server, config_s_get_status());
        break;

    case CONFIG_S_SERVER_EVT_DISCONNECTED:
        __LOG(LOG_SRC_APP, LOG_LEVEL_DBG2, "Config Service: Disconnected\r\n");
        if(CONFIG_S_STATUS_DONE != config_s_get_status()) {
            advertising_start();
        }
        break;

    case CONFIG_S_SERVER_EVT_WRITE: {
        ble_gatts_evt_write_t const *p_evt_write = p_evt->write_evt_params;
        __LOG(LOG_SRC_APP, LOG_LEVEL_DBG2, "Config Service: Write\r\n");

        /** APPKEY characteristic */
        if (p_evt_write->handle == p_config_s_server->char_handles[CONFIG_S_APPKEY].value_handle) {
            if (CONFIG_S_STATUS_WAITING == config_s_get_status()) {
                memcpy(m_blesc_config.app_key, p_evt_write->data, BLEAM_KEY_SIZE);
                __LOG_XB(LOG_SRC_APP, LOG_LEVEL_INFO, "Set appkey to", p_evt_write->data, BLEAM_KEY_SIZE);
                // If node ID was set already
                if(0 != m_blesc_config.node_id) {
                    if(NRF_SUCCESS == flash_config_write()) {
                        config_s_status_update(p_config_s_server, CONFIG_S_STATUS_SET);
                    } else {
                        __LOG(LOG_SRC_APP, LOG_LEVEL_DBG2, "CONFIG FAIL 1\r\n");
                        config_s_status_update(p_config_s_server, CONFIG_S_STATUS_FAIL);
                    }
                }
            } else {
                __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Reset flash to set new keys and node ID.\r\n");
            }
        } else
        /** NODE ID characteristic */
        if (p_evt_write->handle == p_config_s_server->char_handles[CONFIG_S_NODE_ID].value_handle) {
            if (CONFIG_S_STATUS_WAITING == config_s_get_status()) {
                m_blesc_config.node_id = ((uint16_t)p_evt_write->data[0] << 8) | ((uint16_t)p_evt_write->data[1]);
                __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Set node ID to %04X\r\n", m_blesc_config.node_id);
                // If appkey was set already
                if(0 != m_blesc_config.app_key[0] && 0 != m_blesc_config.app_key[1]) {
                    if(NRF_SUCCESS == flash_config_write()) {
                        config_s_status_update(p_config_s_server, CONFIG_S_STATUS_SET);
                    } else {
                        __LOG(LOG_SRC_APP, LOG_LEVEL_DBG2, "CONFIG FAIL 2\r\n");
                        config_s_status_update(p_config_s_server, CONFIG_S_STATUS_FAIL);
                    }
                }
            } else {
                __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Reset flash to set new keys and node ID.\r\n");
            }
        } else
        /** STATUS characteristic */
        if (p_evt_write->handle == p_config_s_server->char_handles[CONFIG_S_STATUS].value_handle) {
            if (CONFIG_S_STATUS_SET == config_s_get_status() && CONFIG_S_STATUS_DONE == p_evt_write->data[0]) {
                // Configuration finished
                config_s_finish();
                err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
                if(NRF_ERROR_INVALID_STATE != err_code)
                    APP_ERROR_CHECK(err_code);
                leave_config_mode();
            } else if (CONFIG_S_STATUS_FAIL == p_evt_write->data[0]) {
                // Configuration failed
                err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
                if(NRF_ERROR_INVALID_STATE != err_code)
                    APP_ERROR_CHECK(err_code);
                memset(&m_blesc_config, 0, sizeof(configuration_t));
                flash_config_delete();
            }
        } else {
            // default
        }
        break;
    }

    default:
        // No implementation needed.
        break;
    }
}

/**@brief Function for battery measurement
 * @ingroup battery
 *
 * @details This function will start the ADC.
 *
 * @returns Nothing.
 */
static void battery_level_measure(void) {
    ret_code_t err_code;

    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Battery level measurement request.\r\n");

#ifndef BOARD_RUUVITAG_B
    err_code = nrf_drv_saadc_sample();
    APP_ERROR_CHECK(err_code);
#else
    float voltage_batt_lvl;
    uint8_t percentage_batt_lvl;

    ruuvi_driver_status_t ruuvi_err_code = RUUVI_DRIVER_SUCCESS;
    ruuvi_driver_sensor_data_t data = {0};
    data.data = &voltage_batt_lvl;
    data.fields.datas.voltage_v = 1;

    ruuvi_err_code |= adc_sensor.data_get(&data);
    if (RUUVI_DRIVER_SUCCESS != ruuvi_err_code) {
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Battery level measurement fail.\r\n");
        return;
    }
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Battery level is at " NRF_LOG_FLOAT_MARKER " V, %d.\r\n", NRF_LOG_FLOAT(voltage_batt_lvl), (int)(voltage_batt_lvl * 10));

    bleam_health_queue_add(voltage_batt_lvl * 10, m_blesc_uptime, m_system_time);
#endif
}

#ifndef BOARD_RUUVITAG_B
/**@brief Function for handling the ADC interrupt.
 * @ingroup battery
 *
 * @details  This function will fetch the conversion result from the ADC, convert the value into
 *           percentage and send it to peer.
 *
 * @returns Nothing.
 */
void saadc_event_handler(nrf_drv_saadc_evt_t const * p_event)
{
    if (p_event->type == NRF_DRV_SAADC_EVT_DONE)
    {
        nrf_saadc_value_t adc_result;
        float             voltage_batt_lvl;
        uint8_t           percentage_batt_lvl;
        uint32_t          err_code;

        adc_result = p_event->data.done.p_buffer[0];
        err_code = nrf_drv_saadc_buffer_convert(p_event->data.done.p_buffer, 1);
        APP_ERROR_CHECK(err_code);

        voltage_batt_lvl = (ADC_RESULT_IN_MILLI_VOLTS(adc_result) + DIODE_FWD_VOLT_DROP_MILLIVOLTS) * 0.001;

        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Battery level is at "NRF_LOG_FLOAT_MARKER" V, %d.\r\n", NRF_LOG_FLOAT(voltage_batt_lvl), (int)(voltage_batt_lvl * 10));
        bleam_health_queue_add(voltage_batt_lvl * 10, m_blesc_uptime, m_system_time);
    }
}
#endif

/**@brief Function for handling Queued Write Module errors.
 * @ingroup blesc_app
 *
 * @details A pointer to this function will be passed to each service which may need to inform the
 *          application about an error.
 *
 * @param[in]   nrf_error   Error code containing information about what went wrong.
 *
 * @returns Nothing.
 */
static void nrf_qwr_error_handler(uint32_t nrf_error) {
    APP_ERROR_HANDLER(nrf_error);
}

/**@brief Function for handling the data from the BLEAM Service.
 * @ingroup bleam_connect
 *
 * @details This function will process the data received from the BLEAM Service
 *
 * @param[in] p_bleam_client       Pointer to the struct of BLEAM service.
 * @param[in] p_evt                BLEAM Service event.
 *
 * @returns Nothing.
 */
static void bleam_service_evt_handler(bleam_service_client_t *p_bleam_client, bleam_service_client_evt_t *p_evt) {
    ret_code_t err_code;

    switch (p_evt->evt_type) {
    case BLEAM_SERVICE_CLIENT_EVT_DISCOVERY_COMPLETE: {
//        app_timer_stop(m_bleam_inactivity_timer_id);
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "BLEAM service event: Service discovery complete\r\n");

        // IOS BLEAM won't send salt if there is already a BLEAM Scanner connection happening
        err_code = app_timer_start(m_bleam_inactivity_timer_id, BLEAM_SERVICE_BLEAM_INACTIVITY_TIMEOUT, NULL);
        APP_ERROR_CHECK(err_code);
        // read salt
        err_code = bleam_service_client_notify_enable(p_bleam_client);
        if(NRF_SUCCESS != err_code) {
            __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Can't enable notify, remote disconnected\r\n");
            err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            if(NRF_ERROR_INVALID_STATE != err_code)
                APP_ERROR_CHECK(err_code);
        }
        break;
    }

    case BLEAM_SERVICE_CLIENT_EVT_RECV_SALT: {
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "BLEAM service event: Received salt\r\n");
        app_timer_stop(m_bleam_inactivity_timer_id);

        uint8_t cmd = p_evt->p_data[0];
        // Received salt for regular BLEAM connect
        if(BLEAM_SERVICE_CLIENT_CMD_SALT == cmd) {
            bleam_service_mode_set(BLEAM_SERVICE_CLIENT_MODE_RSSI);
            uint8_t salt[SALT_SIZE];
            memcpy(salt, p_evt->p_data + 1, SALT_SIZE);
            __LOG_XB(LOG_SRC_APP, LOG_LEVEL_INFO, "Received salt", salt, SALT_SIZE);
            sign_data(m_digest, salt, m_blesc_config.app_key);
            __LOG_XB(LOG_SRC_APP, LOG_LEVEL_INFO, "Signed salt", m_digest, NRF_CRYPTO_HASH_SIZE_SHA256);
            bleam_send_init(p_bleam_client, m_digest);
        // Received first half of BLEAM signature
        } else if(BLEAM_SERVICE_CLIENT_CMD_SIGN1 == cmd) {
            m_bleam_signature_halves |= 1;
            memcpy(m_bleam_signature, p_evt->p_data + 1, SALT_SIZE);
        // Received second half of BLEAM signature
        } else if(BLEAM_SERVICE_CLIENT_CMD_SIGN2 == cmd) {
            m_bleam_signature_halves |= 2;
            memcpy(m_bleam_signature + SALT_SIZE, p_evt->p_data + 1, SALT_SIZE);
            // If signature received is incorrect, disconnect
            if(m_bleam_signature_halves != 3
               || 0 != memcmp(m_digest, m_bleam_signature, NRF_CRYPTO_HASH_SIZE_SHA256)) {
                // TODO: Maybe add to blacklist?
                clear_rssi_data(&bleam_rssi_data[m_bleam_uuid_index]);
                err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
                if(NRF_ERROR_INVALID_STATE != err_code)
                    APP_ERROR_CHECK(err_code);
                break;
            }
            // If signature matches, do da thing
            if(BLEAM_SERVICE_CLIENT_MODE_DFU == bleam_service_mode_get()) {
#ifdef BLESC_DFU
                enter_dfu_mode();
#else
                __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "This firmware does not support DFU.\r\n");
                err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
//                APP_ERROR_CHECK(err_code);
#endif
            } else if(BLEAM_SERVICE_CLIENT_MODE_REBOOT == bleam_service_mode_get()) {
                sd_nvic_SystemReset();
            } else if(BLEAM_SERVICE_CLIENT_MODE_UNCONFIG == bleam_service_mode_get()) {
                flash_config_delete();
            }
        // Received command for other interaction protocol
        } else {
            __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Received NOTIFY command %u\r\n", cmd);
            // Send salt to BLEAM to confirm BLEAM is genuine
            uint8_t salt[BLEAM_MAX_DATA_LEN] = {0};
            err_code = nrf_crypto_rng_vector_generate(salt, SALT_SIZE);
            __LOG_XB(LOG_SRC_APP, LOG_LEVEL_INFO, "Generated salt", salt, SALT_SIZE);
            sign_data(m_digest, salt, m_blesc_config.app_key);
            __LOG_XB(LOG_SRC_APP, LOG_LEVEL_INFO, "Signature for BLEAM to match", m_digest, NRF_CRYPTO_HASH_SIZE_SHA256);
            m_bleam_signature_halves = 0;
            memset(m_bleam_signature, 0, NRF_CRYPTO_HASH_SIZE_SHA256);
            bleam_send_salt(p_bleam_client, salt);
            // Wait for signature in salt
            err_code = app_timer_start(m_bleam_inactivity_timer_id, BLEAM_SERVICE_BLEAM_INACTIVITY_TIMEOUT, NULL);
            APP_ERROR_CHECK(err_code);
            // Prepare for DFU mode, 
            if(BLEAM_SERVICE_CLIENT_CMD_DFU == cmd) {
                __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Received request to enter DFU mode.\r\n");
                bleam_service_mode_set(BLEAM_SERVICE_CLIENT_MODE_DFU);
            // Prepare for node reboot
            } else if(BLEAM_SERVICE_CLIENT_CMD_REBOOT == cmd) {
                __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Received request to reboot.\r\n");
                bleam_service_mode_set(BLEAM_SERVICE_CLIENT_MODE_REBOOT);
            } else if(BLEAM_SERVICE_CLIENT_CMD_UNCONFIG == cmd) {
                __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Received request to unconfigure.\r\n");
                bleam_service_mode_set(BLEAM_SERVICE_CLIENT_MODE_UNCONFIG);
            } else {
                __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Impossible NOTIFY command %u\r\n", cmd);
                clear_rssi_data(&bleam_rssi_data[m_bleam_uuid_index]);
                err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
                if(NRF_ERROR_INVALID_STATE != err_code)
                    APP_ERROR_CHECK(err_code);
                break;
            }
        }
        break;
    }

    case BLEAM_SERVICE_CLIENT_EVT_DONE_SENDING_SIGNATURE: {
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "BLEAM service event: Done sending signature\r\n");

        if(BLEAM_SERVICE_CLIENT_MODE_RSSI == bleam_service_mode_get()) {
            // Collect and send health data
            battery_level_measure();

            // Collect and send RSSI data
            for(uint8_t cnt = 0; APP_CONFIG_RSSI_PER_MSG > cnt; ++cnt) {
                bleam_rssi_queue_add(m_blesc_config.node_id, bleam_rssi_data[m_bleam_uuid_index].rssi[cnt], bleam_rssi_data[m_bleam_uuid_index].aoa[cnt]);
            }
        }
        break;
    }

    case BLEAM_SERVICE_CLIENT_EVT_DONE_SENDING: {
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "BLEAM service event: Done sending data\r\n");
        mac_in_whitelist(bleam_rssi_data[m_bleam_uuid_index].mac, NULL);
        clear_rssi_data(&bleam_rssi_data[m_bleam_uuid_index]);

        // Wind up the clock
        if(m_system_time_needs_update) {
            if(NRF_SUCCESS == bleam_service_client_read_time(p_bleam_client)) {
                break;
            }
        }
        err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
        if(NRF_ERROR_INVALID_STATE != err_code)
            APP_ERROR_CHECK(err_code);
        break;
    }

    case BLEAM_SERVICE_CLIENT_EVT_RECV_TIME: {
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "BLEAM service event: Received time\r\n");
        if(m_system_time_needs_update) {
            system_time_update((uint32_t *)p_evt->p_data);
        }
        err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
        if(NRF_ERROR_INVALID_STATE != err_code)
            APP_ERROR_CHECK(err_code);
        break;
    }

    case BLEAM_SERVICE_CLIENT_EVT_DISCONNECTED: {
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "BLEAM service event: Disconnected\r\n");
        // clear data just in case
        clear_rssi_data(&bleam_rssi_data[m_bleam_uuid_index]);
        bleam_service_mode_set(BLEAM_SERVICE_CLIENT_MODE_NONE);
        bleam_send_uninit();
        break;
    }

    case BLEAM_SERVICE_CLIENT_EVT_SRV_NOT_FOUND: {
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "BLEAM service event: BLEAM service not found\r\n");
//        if(!mac_in_blacklist(bleam_rssi_data[m_bleam_uuid_index].mac))
//            add_mac_in_blacklist(bleam_rssi_data[m_bleam_uuid_index].mac);
        stupid_ios_data.active = 2;
        uint8_t last_scan_index = 0;
        if(0 < bleam_rssi_data[m_bleam_uuid_index].scans_stored_cnt)
            last_scan_index = bleam_rssi_data[m_bleam_uuid_index].scans_stored_cnt - 1;
        stupid_ios_data.rssi = bleam_rssi_data[m_bleam_uuid_index].rssi[last_scan_index];
        stupid_ios_data.aoa = bleam_rssi_data[m_bleam_uuid_index].aoa[last_scan_index];
        memcpy(stupid_ios_data.mac, bleam_rssi_data[m_bleam_uuid_index].mac, BLE_GAP_ADDR_LEN);
        memcpy(stupid_ios_data.bleam_uuid, bleam_rssi_data[m_bleam_uuid_index].bleam_uuid, APP_CONFIG_BLEAM_UUID_SIZE);

        clear_rssi_data(&bleam_rssi_data[m_bleam_uuid_index]);
        err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
        if(NRF_ERROR_INVALID_STATE != err_code)
            APP_ERROR_CHECK(err_code);
        break;
    }

    case BLEAM_SERVICE_CLIENT_EVT_BAD_CONNECTION: {
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "BLEAM service event: Bad connection\r\n");
        clear_rssi_data(&bleam_rssi_data[m_bleam_uuid_index]);
        err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
        if(NRF_ERROR_INVALID_STATE != err_code)
            APP_ERROR_CHECK(err_code);
        break;
    }

    default:
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "BLEAM service unhandled event\r\n");
    }
}

/**@brief Function for handling an event from the Connection Parameters Module.
 * @ingroup bleam_connect
 *
 * @details This function will be called for all events in the Connection Parameters Module
 *          which are passed to the application.
 *
 * @note All this function does is to disconnect. This could have been done by simply setting
 *       the disconnect_on_fail config parameter, but instead we use the event handler
 *       mechanism to demonstrate its use.
 *
 * @param[in] p_evt  Event received from the Connection Parameters Module.
 *
 * @returns Nothing.
 */
static void conn_params_evt_handler(ble_conn_params_evt_t *p_evt) {
    uint32_t err_code;

    if (p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED) {
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Conn params event FAILED\r\n");
        err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
        if(NRF_ERROR_INVALID_STATE != err_code)
            APP_ERROR_CHECK(err_code);
    }
}

/**@brief Function for handling errors from the Connection Parameters module.
 * @ingroup bleam_connect
 *
 * @param[in] nrf_error  Error code containing information about what went wrong.
 *
 * @returns Nothing.
 */
static void conn_params_error_handler(uint32_t nrf_error) {
    APP_ERROR_HANDLER(nrf_error);
}

/**@brief Function for handling received device adv data.
 * @ingroup bleam_scan
 *
 * @details Function pulls device details from adv report. If the found device has BLEAM service UUID,
 *          store the found device UUID and address and send the RSSI data across the mesh network.
 *            
 *
 * @param[in] p_adv_report            Pointer to the adv report.
 *
 * @returns Nothing.
 */
static void process_scan_data(ble_gap_evt_adv_report_t const *p_adv_report) {
    ret_code_t err_code;
    uint8_t idx = 0;
    uint16_t dev_name_offset = 0;
    uint16_t field_len;
    data_t adv_data;
    uint32_t time_diff;

    // Initialize advertisement report for parsing
    adv_data.p_data = (uint8_t *)p_adv_report->data.p_data;
    adv_data.data_len = p_adv_report->data.len;

    uint8_t p_data_uuid[20] = {0};

    for(uint8_t i = 0; p_adv_report->data.len > i; ++i) {
        if(adv_data.p_data[i] == 17 &&
            adv_data.data_len >= i + 1 + adv_data.p_data[i] &&
            adv_data.p_data[i+1] == 0x07) {
           memcpy(p_data_uuid, &adv_data.p_data[i+2], adv_data.p_data[i]-1);
           break;
        }
        i += adv_data.p_data[i];
    }

    // Show BLEAM scans
    if (p_data_uuid[13] == uuid_bleam_to_scan[0] && p_data_uuid[12] == uuid_bleam_to_scan[1]) {
//        __LOG(LOG_SRC_APP, LOG_LEVEL_WARN, "NORMAL BLEAM!\r\n");
        m_bleam_nearby = true;
        app_timer_stop(m_eco_timer_id);

        uint8_t bleam_uuid_to_send[APP_CONFIG_BLEAM_UUID_SIZE];
        for(int i = 1 + APP_CONFIG_BLEAM_UUID_SIZE, j = 0; i > 1;)
            bleam_uuid_to_send[j++] = p_data_uuid[i--];

        if(NULL == mac_in_whitelist(p_adv_report->peer_addr.addr, bleam_uuid_to_send))
            add_mac_in_whitelist(p_adv_report->peer_addr.addr, bleam_uuid_to_send);
        
        // Save device to storage
        const uint8_t uuid_index = app_blesc_save_bleam_to_storage(bleam_uuid_to_send, p_adv_report->peer_addr.addr);
        // If storage is full
        if(APP_CONFIG_MAX_BLEAMS == uuid_index) {
            __LOG(LOG_SRC_APP, LOG_LEVEL_WARN, "BLEAM storage full!\r\n");
            return;
        }
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Scanned RSSI %d\r\n", (int8_t)p_adv_report->rssi);
        uint8_t aoa = 0;
        if(app_blesc_save_rssi_to_storage(uuid_index, &p_adv_report->rssi, &aoa)) {
            scan_stop();
            try_bleam_connect(uuid_index);
        }
        return;
    }

    memset(p_data_uuid, 0, 20);
    for(uint8_t i = 0; p_adv_report->data.len > i; ++i) {
        if(adv_data.p_data[i] == 20 &&
            adv_data.data_len >= i + 1 + adv_data.p_data[i] &&
            adv_data.p_data[i+1] == 0xFF) {
           memcpy(p_data_uuid, &adv_data.p_data[i+2], adv_data.p_data[i]-1);
           break;
        }
        i += adv_data.p_data[i];
    }
    // Apple twist
    if (p_data_uuid[0] == 0x4C && p_data_uuid[1] == 00) {
//        __LOG(LOG_SRC_APP, LOG_LEVEL_WARN, "APPLE TWIST!\r\n");
        uint8_t * bleam_uuid_to_send;
        bleam_uuid_to_send = mac_in_whitelist(p_adv_report->peer_addr.addr, NULL);
        if(NULL != bleam_uuid_to_send) {// Save device to storage
            app_timer_stop(m_eco_timer_id);
            m_bleam_nearby = true;

            const uint8_t uuid_index = app_blesc_save_bleam_to_storage(bleam_uuid_to_send, p_adv_report->peer_addr.addr);
            // If storage is full
            if(APP_CONFIG_MAX_BLEAMS == uuid_index) {
                __LOG(LOG_SRC_APP, LOG_LEVEL_WARN, "BLEAM storage full!\r\n");
                return;
            }
            __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Scanned iOS RSSI %d\r\n", (int8_t)p_adv_report->rssi);
            uint8_t aoa = 0;
            if(app_blesc_save_rssi_to_storage(uuid_index, &p_adv_report->rssi, &aoa)) {
                scan_stop();
                try_bleam_connect(uuid_index);
            }
        } else {
            if(mac_in_blacklist(p_adv_report->peer_addr.addr))
                return;
            app_timer_stop(m_eco_timer_id);
            stupid_ios_data.active = 1;
            memcpy(stupid_ios_data.mac, p_adv_report->peer_addr.addr, BLE_GAP_ADDR_LEN);
            stupid_ios_data.rssi = p_adv_report->rssi;
            stupid_ios_data.aoa = NULL;
            scan_stop();
            try_ios_connect();
        }
    }
}

/**@brief Function for handling Scanning events.
 * @ingroup bleam_scan
 *
 * @param[in]     p_scan_evt     Scanning event.
 *
 * @returns Nothing.
 */
static void scan_evt_handler(scan_evt_t const *p_scan_evt) {
    ret_code_t err_code;

    switch (p_scan_evt->scan_evt_id) {
    case NRF_BLE_SCAN_EVT_CONNECTING_ERROR:
        err_code = p_scan_evt->params.connecting_err.err_code;
        APP_ERROR_CHECK(err_code);
        break;

    case NRF_BLE_SCAN_EVT_CONNECTED: {
        ble_gap_evt_connected_t const *p_connected =
            p_scan_evt->params.connected.p_connected;
        // Scan is automatically stopped by the connection.
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Connecting to target %02x-%02x-%02x-%02x-%02x-%02x\r\n",
            p_connected->peer_addr.addr[5],
            p_connected->peer_addr.addr[4],
            p_connected->peer_addr.addr[3],
            p_connected->peer_addr.addr[2],
            p_connected->peer_addr.addr[1],
            p_connected->peer_addr.addr[0]);
    } break;

    case NRF_BLE_SCAN_EVT_NOT_FOUND:
        process_scan_data(p_scan_evt->params.p_not_found);
        break;
    default:
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Scan over event\r\n");
        break;
    }
}

/** @} end of handlers */

/*************************  INITIALIZERS ***************************/

/**@addtogroup initializers
 * @{
 */

/**@brief Function for initializing the logging module, both @link_lib_nrf_log and nRF Mesh SDK logging.
 * @ingroup blesc_debug
 *
 * @returns Nothing.
 */
static void logging_init(void) {
    ret_code_t err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_DEFAULT_BACKENDS_INIT();
    __LOG_INIT(LOG_SRC_APP | LOG_SRC_FRIEND, APP_CONFIG_LOG_LEVEL, LOG_CALLBACK_DEFAULT);
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "====== Booting ======\nLogging initialised.\r\n");
}

/**@brief Function for initializing the timer module.
 * @ingroup bleam_time
 *
 * @details Besides initialising all main application timers, this function also
 *          sets initial system time values until the time is updated via BLEAM connection.
 *
 * @returns Nothing.
 */
static void timers_init(void) {
    m_system_time = BLESC_DAYTIME_START;
    m_blesc_uptime = 0;
    m_blesc_time_period = BLESC_TIME_PERIODS_DAY * BLESC_TIME_PERIOD_SECS;
    m_system_time_needs_update = true;
    ret_code_t err_code = app_timer_init();
    APP_ERROR_CHECK(err_code);

    // System time timer.
    err_code = app_timer_create(&m_system_time_timer_id, APP_TIMER_MODE_REPEATED, system_time_timer_handler);
    APP_ERROR_CHECK(err_code);

    // Timer for scan/connect cycle    
    err_code = app_timer_create(&scan_connect_timer, APP_TIMER_MODE_SINGLE_SHOT, scan_connect_timer_handle);
    APP_ERROR_CHECK(err_code);

    // BLEAM inactivity timer.
    err_code = app_timer_create(&m_bleam_inactivity_timer_id, APP_TIMER_MODE_SINGLE_SHOT, bleam_inactivity_timeout_handler);
    APP_ERROR_CHECK(err_code);
 
    // Timer for blacklist
    err_code = app_timer_create(&drop_blacklist_timer_id, APP_TIMER_MODE_REPEATED, drop_blacklist);
    APP_ERROR_CHECK(err_code);

    // Eco timer.
    err_code = app_timer_create(&m_eco_timer_id, APP_TIMER_MODE_SINGLE_SHOT, eco_timer_handler);
    APP_ERROR_CHECK(err_code);

    // Eco watchdog timer.
    err_code = app_timer_create(&m_eco_watchdog_timer_id, APP_TIMER_MODE_REPEATED, eco_watchdog_timer_handler);
    APP_ERROR_CHECK(err_code);

    // Start watchdog timer
    app_timer_start(drop_blacklist_timer_id, MACLIST_TIMEOUT, NULL);
    app_timer_start(m_system_time_timer_id, APP_TIMER_TICKS(1000), NULL);
}

#if NRF_MODULE_ENABLED(DEBUG)
  #ifndef BOARD_RUUVITAG_B
/**@brief Function for the LEDs initialization.
 * @ingroup blesc_debug
 *
 * @details Initializes all LEDs used by the application.
 *
 * @returns Nothing.
 */
static void leds_init(void) {
    bsp_board_init(BSP_INIT_LEDS);
    bsp_board_led_on(CENTRAL_CONNECTED_LED);
    bsp_board_led_on(CENTRAL_SCANNING_LED);
}
  #else
/**@brief Function for the LEDs initialization for RuuviTag.
 * @ingroup blesc_debug
 *
 * @details Initializes all LEDs used by the application.
 *
 * @returns Nothing.
 */
static void ruuvi_leds_init(void) {
    ruuvi_interface_gpio_id_t leds[RUUVI_BOARD_LEDS_NUMBER];
    ruuvi_driver_status_t err_code = RUUVI_DRIVER_SUCCESS;
    if(!ruuvi_interface_gpio_is_init())
        err_code = ruuvi_interface_gpio_init();
    if (RUUVI_DRIVER_SUCCESS == err_code) {
        uint16_t pins[] = RUUVI_BOARD_LEDS_LIST;

        for (uint8_t i = 0; RUUVI_BOARD_LEDS_NUMBER > i; ++i) {
            ruuvi_interface_gpio_id_t led;
            led.pin = pins[i];
            leds[i] = led;
            err_code |= ruuvi_interface_gpio_configure(leds[i], RUUVI_INTERFACE_GPIO_MODE_OUTPUT_HIGHDRIVE);
            err_code |= ruuvi_interface_gpio_write(leds[i], RUUVI_BOARD_LEDS_ACTIVE_STATE);
        }
    }

    RUUVI_DRIVER_ERROR_CHECK(err_code, RUUVI_DRIVER_SUCCESS);
}
  #endif

  #ifndef BOARD_RUUVITAG_B
/**@brief Function for initializing buttons.
 * @ingroup blesc_debug
 *
 * @param[out] p_erase_bonds  Will be true if the clear bonding button was pressed to wake the application up.
 *
 * @returns Nothing.
 */
static void buttons_init(bool *p_erase_bonds) {
    bsp_event_t startup_event;

    uint32_t err_code = bsp_init(BSP_INIT_BUTTONS, bsp_event_handler);
    APP_ERROR_CHECK(err_code);

//    err_code = bsp_btn_ble_init(NULL, &startup_event);
//    APP_ERROR_CHECK(err_code);

    *p_erase_bonds = (startup_event == BSP_EVENT_CLEAR_BONDING_DATA);
}
  #else
/**@brief Function for the buttons initialization.
 * @ingroup blesc_debug
 *
 * @details Initializes all buttons used by the application.
 *
 * @returns Nothing.
 */
static void ruuvi_buttons_init(void) {
    ruuvi_driver_status_t err_code = RUUVI_DRIVER_SUCCESS;

    if (!(ruuvi_interface_gpio_is_init() && ruuvi_interface_gpio_interrupt_is_init())) {
        err_code = ruuvi_interface_gpio_init();
        RUUVI_DRIVER_ERROR_CHECK(err_code, RUUVI_DRIVER_SUCCESS);
        err_code = ruuvi_interface_gpio_interrupt_init(interrupt_table, sizeof(interrupt_table));
        RUUVI_DRIVER_ERROR_CHECK(err_code, RUUVI_DRIVER_SUCCESS);
    }
    ruuvi_interface_gpio_id_t pin = {.pin = RUUVI_BOARD_BUTTON_1};
    ruuvi_interface_gpio_slope_t slope = RUUVI_INTERFACE_GPIO_SLOPE_HITOLO;
    ruuvi_interface_gpio_mode_t mode = RUUVI_INTERFACE_GPIO_MODE_INPUT_PULLDOWN;

    if (RUUVI_INTERFACE_GPIO_LOW == RUUVI_BOARD_BUTTONS_ACTIVE_STATE) {
        mode = RUUVI_INTERFACE_GPIO_MODE_INPUT_PULLUP;
    }

    err_code = ruuvi_interface_gpio_interrupt_enable(pin, slope, mode, &ruuvi_button_event_handler);
    RUUVI_DRIVER_ERROR_CHECK(err_code, RUUVI_DRIVER_SUCCESS);
}
  #endif
#endif

#ifdef BLESC_DFU
/**@brief Function for initializing RuuviTag DFU.
 * @ingroup blesc_dfu
 *
 * @returns Nothing.
 */
static void dfu_init (void) {
    // Initialize the async SVCI interface to bootloader before any interrupts are enabled.
    ret_code_t err_code = ble_dfu_buttonless_async_svci_init();
    APP_ERROR_CHECK(err_code);
}
#endif

/**@brief Function for initializing the watchdog.
 * @ingroup blesc_app
 *
 * @returns Nothing.
 */
static void wdt_init() {
    ret_code_t err_code = NRF_SUCCESS;
    nrf_drv_wdt_config_t config = NRF_DRV_WDT_DEAFULT_CONFIG;
    err_code = nrf_drv_wdt_init(&config, wdt_event_handler);
    APP_ERROR_CHECK(err_code);
    err_code = nrf_drv_wdt_channel_alloc(&m_channel_id);
    APP_ERROR_CHECK(err_code);
    nrf_drv_wdt_enable();
}

/**@brief Function for initializing power management.
 * @ingroup blesc_app
 *
 * @returns Nothing.
 */
static void power_management_init(void) {
    ret_code_t err_code;
    err_code = nrf_pwr_mgmt_init();
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing the database discovery module.
 * @ingroup bleam_connect
 *
 * @returns Nothing.
 */
static void db_discovery_init(void)
{
    ret_code_t err_code;
    err_code = bleam_service_discovery_init(bleam_service_discovery_evt_handler, BLEAM_SERVICE_UUID);
    APP_ERROR_CHECK(err_code);
    err_code = ble_db_discovery_init(db_disc_handler);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for the SoftDevice initialization.
 * @ingroup blesc_app
 *
 * @details This function initializes the SoftDevice and the BLE event interrupt.
 *
 * @returns Nothing.
 */
static void ble_stack_init(void) {
    ret_code_t err_code;

    err_code = nrf_sdh_enable_request();
    APP_ERROR_CHECK(err_code);

    // Configure the BLE stack using the default settings.
    // Fetch the start address of the application RAM.
    uint32_t ram_start = 0;
    err_code = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
    APP_ERROR_CHECK(err_code);

    // Enable BLE stack.
    err_code = nrf_sdh_ble_enable(&ram_start);
    APP_ERROR_CHECK(err_code);

    // Register a handler for BLE events.
    NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
    
    // Wait for random bytes
    size_t cnt = 0;
    for (; 100 > cnt; ++cnt) {
        uint8_t bytes;
        sd_rand_application_bytes_available_get(&bytes);
        if(2 > bytes) {
            nrf_delay_ms(100);
        } else
            break;
    }
    if(100 <= cnt) {
        APP_ERROR_CHECK(NRF_ERROR_NO_MEM);
    }
}

#ifndef BOARD_RUUVITAG_B
/**@brief Function for configuring nRF ADC to do battery level conversion.
 * @ingroup battery
 *
 * @returns Nothing.
 */
static void adc_init(void)
{
    ret_code_t err_code = nrf_drv_saadc_init(NULL, saadc_event_handler);
    APP_ERROR_CHECK(err_code);

    nrf_saadc_channel_config_t config =
        NRF_DRV_SAADC_DEFAULT_CHANNEL_CONFIG_SE(NRF_SAADC_INPUT_VDD);
    err_code = nrf_drv_saadc_channel_init(0, &config);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_drv_saadc_buffer_convert(&adc_buf[0], 1);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_drv_saadc_buffer_convert(&adc_buf[1], 1);
    APP_ERROR_CHECK(err_code);
}
#else
/**@brief Function for configuring RuuviTag ADC to do battery level conversion.
 * @ingroup battery
 *
 * @returns Nothing.
 */
static void ruuvi_adc_init(void) {
    ruuvi_driver_status_t err_code = RUUVI_DRIVER_SUCCESS;
    ruuvi_driver_bus_t bus = RUUVI_DRIVER_BUS_NONE;
    uint8_t handle = RUUVI_INTERFACE_ADC_AINVDD;
    ruuvi_driver_sensor_configuration_t config;

    config.samplerate    = APPLICATION_ADC_SAMPLERATE;
    config.resolution    = APPLICATION_ADC_RESOLUTION;
    config.scale         = APPLICATION_ADC_SCALE;
    config.dsp_function  = APPLICATION_ADC_DSPFUNC;
    config.dsp_parameter = APPLICATION_ADC_DSPPARAM;
    config.mode          = APPLICATION_ADC_MODE;

    err_code |= ruuvi_interface_adc_mcu_init(&adc_sensor, bus, handle);
    //RUUVI_DRIVER_ERROR_CHECK(err_code, RUUVI_DRIVER_SUCCESS);

    err_code |= adc_sensor.configuration_set(&adc_sensor, &config);
    //RUUVI_DRIVER_ERROR_CHECK(err_code, ~RUUVI_DRIVER_ERROR_FATAL);
}
#endif

/**@brief Function for the GAP initialization.
 * @ingroup blesc_config
 *
 * @details This function will set up all the necessary GAP (Generic Access Profile) parameters of
 *          the device. It also sets the permissions and appearance.
 *
 * @returns Nothing.
 */
static void gap_params_init(void) {
    uint32_t err_code;
    ble_gap_conn_params_t gap_conn_params;
    ble_gap_conn_sec_mode_t sec_mode;

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

    err_code = sd_ble_gap_device_name_set(&sec_mode,
        (const uint8_t *)DEVICE_NAME,
        strlen(DEVICE_NAME));
    APP_ERROR_CHECK(err_code);

    memset(&gap_conn_params, 0, sizeof(gap_conn_params));

    gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
    gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
    gap_conn_params.slave_latency     = SLAVE_LATENCY;
    gap_conn_params.conn_sup_timeout  = CONN_SUP_TIMEOUT;

    err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing the GATT library.
 * @ingroup bleam_connect
 *
 * @returns Nothing.
 */
static void gatt_init(void) {
    ret_code_t err_code;

    err_code = nrf_ble_gatt_init(&m_gatt, NULL);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_ble_gatt_att_mtu_periph_set(&m_gatt, BLE_GATT_ATT_MTU_DEFAULT);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing basic services that will be used in all BLEAM Scanner modes.
 * @ingroup blesc_app
 *
 * @returns Nothing.
 */
static void basic_services_init(void) {
    uint32_t err_code;
    nrf_ble_qwr_init_t qwr_init = {0};

    // Initialize Queued Write Module.
    qwr_init.error_handler = nrf_qwr_error_handler;
    err_code = nrf_ble_qwr_init(&m_qwr, &qwr_init);
    APP_ERROR_CHECK(err_code);

    // Crypto init
    err_code = nrf_crypto_init();
    APP_ERROR_CHECK(err_code);

    // Random number generation init
    err_code = nrf_crypto_rng_init(NULL, NULL);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing services that will be used by configured BLEAM Scanner.
 * @ingroup bleam_connect
 *
 * @returns Nothing.
 */
static void blesc_services_init(void) {
    uint32_t err_code;
    bleam_service_client_init_t bleam_init = {0};

    // Initialize BLEAM service.
    bleam_init.evt_handler = bleam_service_evt_handler;
    err_code = bleam_service_client_init(&m_bleam_service_client, &bleam_init);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing services that will be used by unconfigured BLEAM Scanner.
 * @ingroup blesc_config
 *
 * @returns Nothing.
 */
static void config_mode_services_init(void) {
    uint32_t err_code;
    config_s_server_init_t config_s_init = {0};

    // Initialise BLEAM Scanner configuration service
    config_s_init.evt_handler = config_s_event_handler;

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&config_s_init.char_attr_md.cccd_write_perm);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&config_s_init.char_attr_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&config_s_init.char_attr_md.write_perm);

    err_code = config_s_server_init(&m_config_service_server, &config_s_init);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing the Advertising functionality.
 * @ingroup blesc_config
 *
 * @returns Nothing.
 */
static void advertising_init(void) {
    ret_code_t    err_code;
    ble_advdata_t advdata     = {0};
    ble_uuid_t    adv_uuids[] = {{CONFIG_S_UUID, BLE_UUID_TYPE_VENDOR_BEGIN}};

    advdata.name_type               = BLE_ADVDATA_FULL_NAME;
//    advdata.include_appearance    = true;
    advdata.flags                   = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
    advdata.uuids_complete.uuid_cnt = sizeof(adv_uuids) / sizeof(adv_uuids[0]);
    advdata.uuids_complete.p_uuids  = adv_uuids;

    err_code = ble_advdata_encode(&advdata, m_adv_data.adv_data.p_data, &m_adv_data.adv_data.len);
    APP_ERROR_CHECK(err_code);

    ble_gap_adv_params_t adv_params;

    // Set advertising parameters.
    memset(&adv_params, 0, sizeof(adv_params));

    adv_params.primary_phy     = BLE_GAP_PHY_1MBPS;
    adv_params.duration        = APP_ADV_DURATION;
    adv_params.properties.type = BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED;
    adv_params.p_peer_addr     = NULL;
    adv_params.filter_policy   = BLE_GAP_ADV_FP_ANY;
    adv_params.interval        = APP_ADV_INTERVAL;

    err_code = sd_ble_gap_adv_set_configure(&m_adv_handle, &m_adv_data, &adv_params);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing the Connection Parameters module.
 * @ingroup bleam_connect
 *
 * @returns Nothing.
 */
static void conn_params_init(void) {
    uint32_t err_code;
    ble_conn_params_init_t cp_init;

    memset(&cp_init, 0, sizeof(cp_init));

    cp_init.p_conn_params                  = NULL;
    cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
    cp_init.next_conn_params_update_delay  = NEXT_CONN_PARAMS_UPDATE_DELAY;
    cp_init.max_conn_params_update_count   = MAX_CONN_PARAMS_UPDATE_COUNT;
    cp_init.start_on_notify_cccd_handle    = BLE_GATT_HANDLE_INVALID;
    cp_init.disconnect_on_fail             = false;
    cp_init.evt_handler                    = conn_params_evt_handler;
    cp_init.error_handler                  = conn_params_error_handler;

    err_code = ble_conn_params_init(&cp_init);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing the Scan module.
 * @ingroup bleam_scan
 *
 * @returns Nothing.
 */
static void scan_init(void) {
    ret_code_t err_code;
    nrf_ble_scan_init_t init_scan = {
        .p_scan_param = &m_scan_params
    };

    init_scan.connect_if_match = true;
    init_scan.conn_cfg_tag = APP_BLE_CONN_CFG_TAG;

    err_code = nrf_ble_scan_init(&m_scan, &init_scan, scan_evt_handler);
    APP_ERROR_CHECK(err_code);
}

/** @} end of initializers */

/**************************** FLASH AND CONFIG *****************************/

/** @addtogroup blesc_fds
 * @{
 */

/**@brief Function for loading config data from FDS, if there is any.
 *
 *@retval NRF_SUCCESS if there is a config data record.
 *@retval NRF_ERROR_NOT_FOUND if there isn't.
 */
static ret_code_t flash_config_load(void) {
    fds_record_desc_t desc = {0};
    fds_find_token_t  tok  = {0};

    ret_code_t err_code = fds_record_find(APP_CONFIG_CONFIG_FILE, APP_CONFIG_CONFIG_REC_KEY, &desc, &tok);

    if (FDS_SUCCESS != err_code) {
        return NRF_ERROR_NOT_FOUND;
    }

    fds_flash_record_t config = {0};
    err_code = fds_record_open(&desc, &config);
    APP_ERROR_CHECK(err_code);
    memcpy(&m_blesc_config, config.p_data, sizeof(configuration_t));

    /* Close the record when done reading. */
    err_code = fds_record_close(&desc);
    APP_ERROR_CHECK(err_code);

    return NRF_SUCCESS;
}

/**@brief Function for creating config data record in FDS.
 *
 *@retval NRF_ERROR_INVALID_STATE if there already exists a config data record.
 *@returns return value of @link_fds_record_write otherwise.
 */
static ret_code_t flash_config_write(void) {
    fds_record_desc_t desc = {0};
    fds_find_token_t  tok  = {0};

    ret_code_t err_code = fds_record_find(APP_CONFIG_CONFIG_FILE, APP_CONFIG_CONFIG_REC_KEY, &desc, &tok);

    if (FDS_SUCCESS == err_code) {
        // Unconfigured node shouldn't have this record
        return NRF_ERROR_INVALID_STATE;
    }

    fds_record_t const config_record = {
        .file_id = APP_CONFIG_CONFIG_FILE,
        .key = APP_CONFIG_CONFIG_REC_KEY,
        .data.p_data = &m_blesc_config,
        /* The length of a record is always expressed in 4-byte units (words). */
        .data.length_words = (sizeof(m_blesc_config) + 3) / sizeof(uint32_t),
    };
    
    err_code = fds_record_write(&desc, &config_record);
//    APP_ERROR_CHECK(err_code);
    return err_code;
}

/**@brief Function for resetting BLEAM Scanner node after successful configuration.
 *
 * @returns Nothing.
 */
static void flash_config_delete(void) {
    // Erase config data from flash
    fds_find_token_t tok = {0};
    fds_record_desc_t desc = {0};

    if (FDS_SUCCESS == fds_record_find(APP_CONFIG_CONFIG_FILE, APP_CONFIG_CONFIG_REC_KEY, &desc, &tok)) {
        ret_code_t err_code = fds_record_delete(&desc);
        APP_ERROR_CHECK(err_code);
        // the rest in is @ref fds_evt_handler under FDS_EVT_DEL_RECORD event
    }
}

/**@brief Function for handling Flash Data Storage events.
 *
 * @param[in]     p_evt     FDS event.
 *
 * @returns Nothing.
 */
static void fds_evt_handler(fds_evt_t const *p_evt) {
    switch (p_evt->id) {
    case FDS_EVT_INIT:
        __LOG(LOG_SRC_APP, LOG_LEVEL_DBG2, "FDS event: INIT\r\n");
        fds_stat_t stat = {0};
        fds_stat(&stat);
        __LOG(LOG_SRC_APP, LOG_LEVEL_DBG2, "FDS statistics:\ncorruption - %d\nvalid - %d\npages - %d\r\n",
            stat.corruption, stat.valid_records, stat.pages_available);
        if (p_evt->result == FDS_SUCCESS) {
            m_fds_initialized = true;

            __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Looking for configuration record...\r\n", m_blesc_config.node_id);

            // Check if node has config data saved in flash
            ret_code_t err_code = flash_config_load();
            if (NRF_SUCCESS == err_code) {
                blesc_services_init();
                config_s_finish();
                conn_params_init();
                scan_init();
                __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "BLESc is starting with Node ID %04X.\r\n", m_blesc_config.node_id);
                scan_start();
            } else { // if (NRF_ERROR_NOT_FOUND == err_code)
                config_mode_services_init();
                conn_params_init();
                advertising_init();
                __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "BLESc is waiting to be configured.\r\n");
                advertising_start();
                app_timer_start(m_eco_watchdog_timer_id, APP_TIMER_TICKS(1500), NULL);
            }
        }
        break;

    case FDS_EVT_WRITE: {
        __LOG(LOG_SRC_APP, LOG_LEVEL_DBG2, "FDS event: WRITE\r\n");
        if (p_evt->result == FDS_SUCCESS) {
            __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "FDS record created.\r\n");
        }
    } break;

    case FDS_EVT_DEL_RECORD: {
        __LOG(LOG_SRC_APP, LOG_LEVEL_DBG2, "FDS event: DEL_RECORD\r\n");
        if (p_evt->result == FDS_SUCCESS) {
            __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "FDS config data cleared.\r\n");
            scan_stop();
            app_timer_stop_all();
            blesc_toggle_leds(0, 0);
            __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "====== System restart ======\r\n");
            nrf_delay_ms(100);
            sd_nvic_SystemReset();
        }
    } break;

    default:
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "FDS event: unhandled\r\n");
        break;
    }
}

/**@brief Function for erasing all flash pages.
 * @details This function erases all flash pages in case they couldn't be initialised.
 *          It commonly occurs after a big DFU update, like from RuuviTag firmware
 *          to BLEAM Scanner bootloader + SD to BLEAM Scanner firmware.
 *
 * @returns Nothing.
 */
void flash_pages_erase(void) {
    ret_code_t err_code = NRF_SUCCESS;
    uint32_t const bootloader_addr = BOOTLOADER_ADDRESS;
    uint32_t const page_sz         = NRF_FICR->CODEPAGESIZE;
    uint32_t const code_sz         = NRF_FICR->CODESIZE;
    uint32_t end_addr = (bootloader_addr != 0xFFFFFFFF) ? bootloader_addr : (code_sz * page_sz);
    end_addr = end_addr - (FDS_PHY_PAGES_RESERVED * FDS_PHY_PAGE_SIZE * sizeof(uint32_t));

    uint32_t cur_page = 0;

    for (uint32_t i = 1; i <= FDS_PHY_PAGES - FDS_PHY_PAGES_RESERVED; ++i) {
        cur_page = (end_addr / (sizeof(uint32_t) * FDS_PHY_PAGE_SIZE)) - i;
        do {
            err_code = sd_flash_page_erase(cur_page);
        } while(NRF_ERROR_BUSY == err_code);
        APP_ERROR_CHECK(err_code);
    }
}

/**@brief Function for initialising Flash Data Storage module.
 *
 * @returns Nothing.
 */
static void flash_init() {
    /* Register first to receive an event when initialization is complete. */
    (void) fds_register(fds_evt_handler);

    ret_code_t err_code = fds_init();
    if(FDS_ERR_NO_PAGES == err_code) {
        flash_pages_erase();
        err_code = fds_init();
    }
    APP_ERROR_CHECK(err_code);
}
/** @} end of blesc_fds*/

/**@brief Function for resetting BLEAM Scanner node after successful configuration.
 * @ingroup blesc_config
 *
 * @returns Nothing.
 */
static void leave_config_mode(void) {
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "BLESc is leaving config mode.\r\n");
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "====== System restart ======\r\n");
    sd_nvic_SystemReset();
}

/***********************  MAIN  **********************/


/**@brief Application main function.
 */
int main(void) {

    // Initialize.
    logging_init();
    ble_stack_init();
    blesc_error_on_boot();

    timers_init();

#ifndef BOARD_RUUVITAG_B
#if NRF_MODULE_ENABLED(DEBUG)
    bool erase_bonds;
    buttons_init(&erase_bonds);
    leds_init();
#endif
    adc_init();
#else
#if NRF_MODULE_ENABLED(DEBUG)
    ruuvi_buttons_init();
    ruuvi_leds_init();
#endif
    ruuvi_adc_init();
#endif
#ifdef BLESC_DFU
    dfu_init();
#endif
    wdt_init();
    power_management_init();
    db_discovery_init();
    gap_params_init();
    gatt_init();
    basic_services_init();
    flash_init();
    // All following inits are in @ref fds_evt_handler

    // Enter main loop.
    for (;;) {
        idle_state_handle();
    }
}
