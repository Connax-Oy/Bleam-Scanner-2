/**@defgroup global_app_config Global app configuration
 * @{
 *
 * @brief Macro definitions of BLEAM Scanner aplication parameters.
 *
 * @details Most of definitions here fall under different modules.
 */

#ifndef GLOBAL_APP_CONFIG_H__
#define GLOBAL_APP_CONFIG_H__

#include <stdbool.h>

/** Enable debug for LEDs, logs and BLEAM Scanner advertising */
#define DEBUG_ENABLED 1
#if !DEBUG_ENABLED
//#if defined(BLESC_DFU) || DEBUG_ENABLED
  #ifdef NRF_LOG_DEFAULT_LEVEL
    #undef NRF_LOG_DEFAULT_LEVEL
  #endif
  // NRF_LOG setting
// <0=> Off
// <1=> Error
// <2=> Warning
// <3=> Info
// <4=> Debug
  #define NRF_LOG_DEFAULT_LEVEL 1
  // __LOG setting
// <0=> assertions
// <1=> error messages.
// <2=> warning messages.
// <3=> report messages.
// <4=> information messages.
// <5=> debug messages (debug level 1)...
  #define APP_CONFIG_LOG_LEVEL 1 /**< Logging level for the application. */
#else
  #define APP_CONFIG_LOG_LEVEL 5 /**< Logging level for the application. */
#endif

#define APP_CONFIG_DEVICE_NAME             "BLESc" /**< Name of device. Will be included in the advertising data. */
#define APP_CONFIG_PROTOCOL_NUMBER         2       /**< BLEAM Scanner protocol number. */
#define APP_CONFIG_FW_VERSION_ID           8       /**< Firmware version ID. */

#define APP_CONFIG_BLEAM_SERVICE_UUID      0xB500  /**<@ingroup bleam_service
                                                     * UUID of BLEAM device. */
#define APP_CONFIG_CONFIG_SERVICE_UUID     0xB700  /**<@ingroup blesc_config
                                                     * UUID of BLEAM Scanner configuration service. */

#define APP_CONFIG_SCAN_CONNECT_INTERVAL    10000   /**< Maximum time BLEAM Scanner can spend scanning before it tries to connect, ms. */
#define APP_CONFIG_BLEAM_INACTIVITY_TIMEOUT 3000    /**< Maximum inactivity time after BLEAM connection before BLEAM Scanner disconnects, ms. */
#define APP_CONFIG_MACLIST_TIMEOUT          30000   /**< Expiry timeout for MAC whitelist/blacklist entries, ms. */
#define APP_CONFIG_RSSI_FILTER_INTERVAL     200     /**< Time interval for RSSI scan timeout before connect, ms. */

/** @}*/

/**@addtogroup bleam_time
 * @{
 */

// Period is a time segment bound to real time. All BLEAM Scanners have to be awake at the start of each period.

#define BLESC_TIME_PERIOD_SECS            10        /**< Number of seconds in a period BLEAM Scanner scanners will try to sync by */
#define BLESC_TIME_PERIODS_DAY            1         /**< Number of @ref BLESC_TIME_PERIOD_SECS in a day cycle, for systemwide sync */
#define BLESC_TIME_PERIODS_NIGHT          6         /**< Number of @ref BLESC_TIME_PERIOD_SECS in a night cycle, for systemwide sync */

#define APP_CONFIG_ECO_SCAN_SECS          1         /**< Time interval for BLEAM Scanner to scan for BLEAMs between sleeps, seconds. */

#define TIME_TO_SEC(_h, _m, _s)           (_h*60*60 + _m*60 + _s)  /**< Macro to convert 24-hour H:M:S time to seconds since midnight */
#define BLESC_DAYTIME_START               TIME_TO_SEC(6, 0, 0)     /**< System time that corresponds with start of the day */
#define BLESC_NIGHTTIME_START             TIME_TO_SEC(1, 0, 0)   /**< System time that corresponds with start of the night */

/** @} end of bleam_time */

/**@addtogroup bleam_storage
 * @{
 */
#define APP_CONFIG_MAX_BLEAMS           8       /**< Size of detected devices' RSSI data storage array */
#define APP_CONFIG_BLEAM_UUID_SIZE      10      /**< Length of the unique BLEAM UUID part */
#define APP_CONFIG_RSSI_PER_MSG         5       /**< Number of RSSI scan results per message to BLEAM */
#define BLEAM_KEY_SIZE                 (16)     /**< Size (in octets) of a BLEAM application key.*/
/** @} end of bleam_storage */

/**@addtogroup blesc_fds
 * @{
 */
/* File ID and Key used for the configuration record. */
#define APP_CONFIG_CONFIG_FILE            (0x1234) /**< Configuration data FDS file ID */
#define APP_CONFIG_CONFIG_REC_KEY         (0x5789) /**< Configuration data FDS record key */
/** @} end of blecs_fds */

#endif /* GLOBAL_APP_CONFIG_H__ */