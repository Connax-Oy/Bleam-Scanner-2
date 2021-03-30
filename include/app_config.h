#ifndef APP_CONFIG_H__
#define APP_CONFIG_H__

#include <stdbool.h>

/**
 * @defgroup APP_SPECIFIC_DEFINES Application-specific macro definitions
 *
 * @{
 */

/** DEBUG definition to call the debug error handler function */
#define DEBUG 1

/** Controls if the model instance should force all mesh messages to be segmented messages. */
#define APP_CONFIG_FORCE_SEGMENTATION  (false)

/** Controls the MIC size used by the model instance for sending the mesh messages. */
#define APP_CONFIG_MIC_SIZE            (NRF_MESH_TRANSMIC_SIZE_SMALL)

/** @} end of APP_SPECIFIC_DEFINES */

/**
 * @defgroup APP_SDK_CONFIG SDK configuration
 *
 * Application-specific SDK configuration settings are provided here.
 *
 * @{
 */

#ifdef BLESC_DFU
  #define NRF_DFU_BLE_BUTTONLESS_SUPPORTS_BONDS 0
#endif
  #define NRF_DFU_TRANSPORT_BLE 1

/** Override default sdk_config.h values. */


/** Enable logging module. */
#ifndef NRF_MESH_LOG_ENABLE
#define NRF_MESH_LOG_ENABLE 1
#endif

/** Default log level. Messages with lower criticality is filtered. */
#ifndef LOG_LEVEL_DEFAULT
#define LOG_LEVEL_DEFAULT LOG_LEVEL_WARN
#endif

/** Default log mask. Messages with other sources are filtered. */
#ifndef LOG_MSK_DEFAULT
#define LOG_MSK_DEFAULT LOG_GROUP_STACK
#endif

/** Enable logging with RTT callback. */
#ifndef LOG_ENABLE_RTT
#define LOG_ENABLE_RTT 1
#endif

/** The default callback function to use. */
#ifndef LOG_CALLBACK_DEFAULT
#if defined(NRF51) || defined(NRF52_SERIES)
    #define LOG_CALLBACK_DEFAULT log_callback_rtt
#else
    #define LOG_CALLBACK_DEFAULT log_callback_stdout
#endif
#endif

#define WDT_ENABLED 1

#ifdef BOARD_RUUVITAG_B
#include "application_driver_configuration.h"
#endif

#define FDS_ENABLED 1
#define NRF_FSTORAGE_ENABLED FDS_ENABLED
#define FDS_VIRTUAL_PAGES 6
#define NRF_DFU_APP_DATA_AREA_SIZE (CODE_PAGE_SIZE * FDS_VIRTUAL_PAGES)

/** Configuration for the BLE SoftDevice support module to be enabled. */
#ifndef BOARD_RUUVITAG_B
  #define NRF_LOG_ENABLED 1
  #define NRF_SDH_ENABLED 1
  #define NRF_SDH_BLE_ENABLED 1
  #define NRF_SDH_SOC_ENABLED 1
  #define NRF_BLE_CONN_PARAMS_ENABLED 1
#endif
#define NRF_SDH_BLE_GATT_MAX_MTU_SIZE 23
#define NRF_SDH_BLE_GAP_DATA_LENGTH 27
#define NRF_SDH_BLE_PERIPHERAL_LINK_COUNT 1
#define NRF_SDH_BLE_SERVICE_CHANGED 1
#define NRF_QUEUE_ENABLED 1

#define SAADC_ENABLED 1

#define NRF_CRYPTO_ENABLED 1
#define NRF_CRYPTO_HMAC_ENABLED 1
#define NRF_CRYPTO_BACKEND_OBERON_ENABLED 1
#define NRF_CRYPTO_BACKEND_NRF_HW_RNG_ENABLED 1
#define RNG_ENABLED 1
#define NRF_CRYPTO_RNG_STATIC_MEMORY_BUFFERS_ENABLED 1
#define NRF_CRYPTO_RNG_AUTO_INIT_ENABLED 0

#ifndef BOARD_RUUVITAG_B
#define APP_TIMER_ENABLED 1
#endif
#define APP_TIMER_KEEPS_RTC_ACTIVE 1

#ifdef APP_TIMER_CONFIG_RTC_FREQUENCY
#undef APP_TIMER_CONFIG_RTC_FREQUENCY
#endif
#define APP_TIMER_CONFIG_RTC_FREQUENCY 0


/** @} end of APP_SDK_CONFIG */

#endif /* APP_CONFIG_H__ */