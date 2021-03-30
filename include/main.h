/************* Doxygen modules descriptions *************/

/**
 * @defgroup blesc_fds Flash Data Storage interface
 * @brief nRF SDK @link_lib_fds operations and handlers for BLEAM Scanner.
 *
 * @details BLEAM Scanner stores some of its data, like @ref blesc_config data,
 *          in flash permanently.
 *
 *          For logic behind flash data storage usage, please refer to @link_wiki_fds.
 */

/**
 * @defgroup blesc_config Configuration
 * @brief BLEAM Scanner node configuration.
 *
 * @details BLEAM Scanner node needs to identify itself within a BLEAM system and
 *          obtain a key to help confirm its identity (@ref bleam_security) on data exchange with BLEAM.
 *
 *          Configuration is a process of unconfigured BLEAM Scanner node receiving
 *          a node ID and an application key.
 *
 *          For details, please refer to @link_wiki_config.
 */

/**
 * @defgroup bleam_scan Scanning for BLEAM
 * @brief BLEAM Scanner's scanning for BLEAM.
 *
 * @details BLEAM Scanner's key functionality is scanning for BLE packets from BLEAM devices.
 *
 *          For details, please refer to @link_wiki_scan.
 */

/**
 * @defgroup bleam_connect Connecting to BLEAM
 * @brief Connection to BLEAM devices.
 *
 * @details BLEAM Scanner connects to BLEAM device in order to share the scanned data.
 *
 *          For details, please refer to @link_wiki_connect.
 */

/**
 * @defgroup bleam_storage Storage of BLEAM data
 * @brief Data structures that hold scanned BLEAM data and functions that operate the structures.
 */

/**
 * @defgroup bleam_security BLEAM security
 * @brief Signature generation and verification.
 *
 * @details More about that at @link_wiki_security.
 */

/**
 * @defgroup bleam_time BLEAM system time
 * @brief Support of system time on a BLEAM Scanner node.
 *
 * @details BLEAM Scanner supports synced system time for better maintenance.
 *
 *          For details, please refer to @link_wiki_time.
 */

/**
 * @defgroup ios_solution iOS problem solution
 * @brief Solution of iOS-specific background advertising problem.
 *
 * @details Apple devices don't explicitly advertise services in background;
 *          rather, they advertise Apple-specific info that is incomprehencible
 *          to third party BLE devices.
 *
 *          This module is a solution to detecting BLEAM service running
 *          in the background on an iOS device.
 *          For details, please refer to @link_wiki_ios.
 */

/**
 * @defgroup blesc_error BLEAM Scanner custom error handler
 * @brief BLEAM Scanner custom error handler
 *
 * @details BLEAM scanner implements a custom error handler in order to
 *          **retain error data** after emergency reset and report it later within
 *          a health status.
 *
 *          For details, please refer to @link_wiki_error.
 */

/**
 * @defgroup blesc_dfu Device firmware update interface
 * @brief nRF SDK @link_lib_bootloader operations and handlers for BLEAM Scanner.
 *
 * @details BLEAM Scanner supports bootloader in order to perform a DFU over BLE.
 *          BLEAM Scanner is developed to be compatible with @link_example_bootloader.
 *
 *          When developing for RuuviTag, refer to the tutorial @link_ruuvi_bootloader.
 */

/**
 * @defgroup battery Battery level measuring
 * @brief BLEAM Scanner battery level measuring for Health status.
 *
 * @details For details, please refer to @link_wiki_battery.
 */

/**
 * @defgroup blesc_app Other BLEAM Scanner application members
 * @brief Softdevice, power manager, idling, watchdog and other important BLEAM Scanner non-modules.
 */

/**
 * @defgroup blesc_debug Debug, LEDs and buttons
 * @brief BLEAM Scanner debugging, button control and LED indication.
 *
 * @details For details, please refer to @link_wiki_debug.
 */

/**
 * @defgroup handlers Event handlers
 * @brief All event handlers from the main application.
 */

/**
 * @defgroup initializers Initialisation functions
 * @brief All initializers from the main application.
 */

#ifndef MAIN_H__
#define MAIN_H__

#include "blesc_error.h"
#include "app_config.h"
#include "app_timer.h"
#include "app_util_platform.h"
#include "global_app_config.h"

/* BLE */
#include "ble_advdata.h"
#include "ble_conn_params.h"
#include "ble_conn_state.h"
#include "ble_db_discovery.h"
#include "bleam_service_discovery.h"
#include "ble_hci.h"
#include "bleam_service.h"
#include "config_service.h"
#include "bleam_send_helper.h"

/* Button module */
#include "bsp.h"

/* Core */
#include "nordic_common.h"
#include "nrf.h"
#include "nrf_ble_gatt.h"
#include "nrf_ble_qwr.h"
#include "nrf_ble_scan.h"
#include "nrf_delay.h"
#include "nrf_drv_saadc.h"
#include "nrf_pwr_mgmt.h"
#include "nrf_power.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"

/* FDS */
#include "fds.h"
#include "fds_internal_defs.h"
#include "nrf_nvmc.h"

#ifdef BLESC_DFU
/* DFU */
#include "ble_dfu.h"
#include "nrf_dfu_ble_svci_bond_sharing.h"
#include "nrf_bootloader_info.h"
#include "peer_manager.h"
#include "peer_manager_handler.h"
#endif

/* Timer */
#include "app_timer.h"
#include "nrf_drv_wdt.h"
#include "nrf_drv_clock.h"

/* Crypto */
#include "nrf_crypto_init.h"
#include "nrf_crypto.h"

/* Ruuvi drivers */
#ifdef BOARD_RUUVITAG_B
#include "ruuvi_boards.h"
#include "ruuvi_driver_error.h"
#include "ruuvi_driver_sensor.h"
#include "ruuvi_interface_gpio.h"
#include "ruuvi_interface_adc.h"
#include "ruuvi_interface_adc_mcu.h"
#include "ruuvi_interface_log.h"
#include "ruuvi_application_config.h"

#include "ruuvi_interface_yield.h"
#include "ruuvi_interface_gpio.h"
#include "ruuvi_interface_gpio_interrupt.h"
#include "ruuvi_interface_acceleration.h"
#endif

/* Logging */
#include "log.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

/**@ingroup blesc_app
 * BLEAM Scanner state */
typedef enum {
    BLESC_STATE_IDLE = 0x00, /**< Idle state, no scanning, no connecting */
    BLESC_STATE_SCANNING,    /**< Scanning state */
    BLESC_STATE_CONNECT,     /**< Connecting state */
} blesc_state_t;

#define CONFIG_APP_KEY_SIZE (BLEAM_KEY_SIZE + 4 - (BLEAM_KEY_SIZE + 2) % 4) /**<@ingroup blesc_config
                                                                             * Size of app_key array in @ref configuration_t struct, so that the struct size in bytes is divisible by 4 */

/**@ingroup blesc_config
 * Configuration data */
typedef struct {
    uint16_t node_id;                      /**< BLEAM Scanner node ID */
    uint8_t  app_key[CONFIG_APP_KEY_SIZE]; /**< BLEAM Scanner node application key */
} configuration_t;

/**@ingroup bleam_storage
 * Detected devices' RSSI data storage struct
 */
typedef struct {
    uint8_t  active;                                 /**< Flag that denotes if this structure is empty or not */
    uint8_t  bleam_uuid[APP_CONFIG_BLEAM_UUID_SIZE]; /**< Bleam UUID for which the RSSI data is collected */
    uint8_t  mac[BLE_GAP_ADDR_LEN];                  /**< Bleam MAC address for which the RSSI data is collected */
    uint8_t  scans_stored_cnt;                       /**< Number of scans in RSSI storage */
    int8_t   rssi[APP_CONFIG_RSSI_PER_MSG];          /**< Received Signal Strength of BLEAM */
    uint8_t  aoa[APP_CONFIG_RSSI_PER_MSG];           /**< Angle of arrival of BLEAM signal */
    uint32_t timestamp;                              /**< Timestamp of last received RSSI */
} blesc_model_rssi_data_t;

/** @ingroup ios_solution
 * iOS RSSI data struct
 */
typedef struct {
    uint8_t  active;                                    /**< Flag that denotes if this structure is empty or not */
    uint8_t  bleam_uuid[APP_CONFIG_BLEAM_UUID_SIZE]; /**< Bleam UUID for which the RSSI data is collected */
    uint8_t  mac[BLE_GAP_ADDR_LEN];                     /**< Bleam MAC address for which the RSSI data is collected */
    int8_t   rssi;                                      /**< Received Signal Strength of BLEAM */
    uint8_t  aoa;                                       /**< Angle of arrival of BLEAM signal */
    uint32_t timestamp;                                 /**< Timestamp of last received RSSI */
} bleam_ios_rssi_data_t;

/** @ingroup ios_solution
 * iOS MAC whitelist data struct
 */
typedef struct {
    uint8_t  active;                                    /**< Flag that denotes if this structure is empty or not */
    uint8_t  bleam_uuid[APP_CONFIG_BLEAM_UUID_SIZE]; /**< Bleam UUID for which the RSSI data is collected */
    uint8_t  mac[BLE_GAP_ADDR_LEN];                     /**< Bleam MAC address for which the RSSI data is collected */
    uint32_t timestamp;                                 /**< Timestamp of last received RSSI */
} bleam_ios_mac_whitelist_t;

/** @ingroup ios_solution
 * iOS MAC blacklist data struct
 */
typedef struct {
    uint8_t  active;                /**< Flag that denotes if this structure is empty or not */
    uint8_t  mac[BLE_GAP_ADDR_LEN]; /**< Bleam MAC address for which the RSSI data is collected */
    uint32_t timestamp;             /**< Timestamp of last received RSSI */
} bleam_ios_mac_blacklist_t;

#define RTC_MAX_TICKS                  APP_TIMER_MAX_CNT_VAL                              /**< Maximum counter value that can be returned by @ref app_timer_cnt_get. */

/**@addtogroup blesc_debug
 * @{ */
#ifndef BOARD_RUUVITAG_B
  #define CENTRAL_SCANNING_LED         BSP_BOARD_LED_0                                    /**< Scanning LED will be on when the device is scanning. */
  #define CENTRAL_CONNECTED_LED        BSP_BOARD_LED_1                                    /**< Connected LED will be on when the device is connected. */
#else
  #define CENTRAL_SCANNING_LED         RUUVI_BOARD_LED_GREEN                              /**< Scanning LED will be on when the device is scanning. */
  #define CENTRAL_CONNECTED_LED        RUUVI_BOARD_LED_RED                                /**< Connected LED will be on when the device is connected. */
#endif
#define BUTTON_DETECTION_DELAY         APP_TIMER_TICKS(50)                                /**< Delay from a GPIOTE event until a button is reported as pushed (in number of timer ticks). */
/** @} end of blesc_debug */

/**@addtogroup blesc_app
 * @{ */
/* BLE stack init macros */
#define APP_BLE_CONN_CFG_TAG           1                                                  /**< A tag identifying the SoftDevice BLE configuration. */
#define APP_BLE_OBSERVER_PRIO          3                                                  /**< Application's BLE observer priority. You shouldn't need to modify this value. */
/** @} end of blesc_app */

/**@addtogruop blesc_config
 * @{ */

/* GAP init macros */
#define DEVICE_NAME                    APP_CONFIG_DEVICE_NAME                             /**< Name of device. Will be included in the advertising data. */
#ifndef BOARD_RUUVITAG_B
  #define MIN_CONN_INTERVAL            MSEC_TO_UNITS(150, UNIT_1_25_MS)                   /**< Minimum acceptable connection interval. */
  #define MAX_CONN_INTERVAL            MSEC_TO_UNITS(250, UNIT_1_25_MS)                   /**< Maximum acceptable connection interval. */
  #define SLAVE_LATENCY                0                                                  /**< Slave latency. */
  #define CONN_SUP_TIMEOUT             MSEC_TO_UNITS(4000, UNIT_10_MS)                    /**< Connection supervisory timeout (4 seconds), Supervision Timeout uses 10 ms units. */
#endif

#define APP_ADV_INTERVAL               800                                                /**< The advertising interval (in units of 0.625 ms. This value corresponds to 500 ms). */
#define APP_ADV_DURATION               BLE_GAP_ADV_TIMEOUT_GENERAL_UNLIMITED              /**< Disable advertising timeout. */
/** @} end of blesc_config */

/** @addtogroup bleam_connect
 * @{ */
#define FIRST_CONN_PARAMS_UPDATE_DELAY APP_TIMER_TICKS(5000)                              /**< Time from initiating event (connect or start of notification) to first time sd_ble_gap_conn_param_update is called (5 seconds). */
#define NEXT_CONN_PARAMS_UPDATE_DELAY  APP_TIMER_TICKS(30000)                             /**< Time between each call to sd_ble_gap_conn_param_update after the first call (30 seconds). */
#define MAX_CONN_PARAMS_UPDATE_COUNT   3                                                  /**< Number of attempts before giving up the connection parameter negotiation. */
/** @} end of bleam_connect */
static void try_bleam_connect(uint8_t p_index);

/** @addtogroup bleam_scan
 * @{ */

/** Advertising data struct for @ref process_scan_data() */
typedef struct
{
    uint8_t                            *p_data;                                           /**< Pointer to data. */
    uint16_t                           data_len;                                          /**< Length of data. */
} data_t;

#define BLESC_SCAN_TIME                APP_TIMER_TICKS((APP_CONFIG_ECO_SCAN_SECS * 1000)) /**< Time for BLEAM Scanner to scan for BLEAMs between sleeps */
#define RSSI_FILTER_TIMEOUT            APP_TIMER_TICKS(APP_CONFIG_RSSI_FILTER_INTERVAL)   /**< Minimum time between received RSSI scans from one device. */
#define SCAN_CONNECT_TIME              APP_TIMER_TICKS(APP_CONFIG_SCAN_CONNECT_INTERVAL)  /**< Time for BLEAM RSSI scan process. */
#define MACLIST_TIMEOUT                APP_TIMER_TICKS(APP_CONFIG_MACLIST_TIMEOUT)        /**< Time for BLEAM RSSI scan process. */
#define SCAN_INTERVAL                  0x00A0                                             /**< Determines scan interval in units of 0.625 millisecond. */
#define SCAN_WINDOW                    0x0050                                             /**< Determines scan window in units of 0.625 millisecond. */
#define SCAN_DURATION                  0x0000                                             /**< Timout when scanning in units if 10 ms. 0x0000 disables timeout. */
#define CONNECT_TIMEOUT                0x012C                                             /**< Timout when connecting in units of 10 ms. 0x0000 disables timeout. */
/** @} end of bleam_scan */


/** @addtogroup battery
 * @{ */

#define ADC_REF_VOLTAGE_IN_MILLIVOLTS  600                                                /**< Reference voltage (in milli volts) used by ADC while doing conversion. */
#define ADC_PRE_SCALING_COMPENSATION   6                                                  /**< The ADC is configured to use VDD with 1/3 prescaling as input. And hence the result of conversion is to be multiplied by 3 to get the actual value of the battery voltage.*/
#define DIODE_FWD_VOLT_DROP_MILLIVOLTS 270                                                /**< Typical forward voltage drop of the diode . */
#define ADC_RES_10BIT                  1024                                               /**< Maximum digital value for 10-bit ADC conversion. */
/**@brief Macro to convert the result of ADC conversion in millivolts.
 *
 * @param[in]  ADC_VALUE   ADC result.
 *
 * @retval     Result converted to millivolts.
 */
#define ADC_RESULT_IN_MILLI_VOLTS(ADC_VALUE)\
        ((((ADC_VALUE) * ADC_REF_VOLTAGE_IN_MILLIVOLTS) / ADC_RES_10BIT) * ADC_PRE_SCALING_COMPENSATION)
static nrf_saadc_value_t adc_buf[2];                                                      /**< ADC buffer. */
#ifdef BOARD_RUUVITAG_B
  static ruuvi_driver_sensor_t adc_sensor = {0};                                          /**< ADC sensor for Ruuvi. */
#endif
/** @} end of battery */

/** @addtogroup bleam_security
 * @{ */
#define SIGN_KEY_MIN_SIZE              2                                                  /**< Minimal size of key for signing. */
#define SIGN_KEY_MAX_SIZE              128                                                /**< Maximal size of key for signing. */
#define SALT_SIZE                      16                                                 /**< Size of salt for signing. */
#define HEX_MAX_BUF_SIZE               2 + (SIGN_KEY_MAX_SIZE << 1)                       /**< Maximal size of hex buffer for signing. */
/** @} end of bleam_security */

/* Misc */
#define DEAD_BEEF                      0xDEADBEEF                                         /**<@ingroup blesc_app
                                                                                            * Value used as error code on stack dump, can be used to identify stack location on stack unwind. */

#endif /* MAIN_H__ */