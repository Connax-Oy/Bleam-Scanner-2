/**
 * @addtogroup bleam_send_helper
 * @{
 */

#ifndef BLEAM_SEND_HELPER_H__
#define BLEAM_SEND_HELPER_H__

#include "bleam_service.h"

#define BLEAM_QUEUE_SIZE 20 /**< Size of the queue array */

/**@brief BLEAM RSSI data structure. */
typedef struct {
    uint16_t sender_id;                                   /**< ID of the RSSI scan data's original sender node */
    int8_t   rssi;                                        /**< Received Signal Strength of BLEAM */
    uint8_t  aoa;                                         /**< Angle of arrival of BLEAM signal */
} bleam_service_rssi_data_t;

/** @brief Health general data struct
 */
typedef struct __attribute((packed)) {
    uint8_t       msg_type;    /**< Flag that signifies this is a general health message. Always should be 0x01 */
    uint8_t       battery_lvl; /**< Battery level in centivolts */
    uint16_t      fw_id;       /**< BLEAM Scanner firmware version number */
    uint32_t      uptime;      /**< Node uptime in minutes */
    uint32_t      system_time; /**< BLEAM Scanner system time in seconds passed since midnight */
    uint16_t      err_id;      /**< Latest error ID that is randomly generated. */
    blesc_error_t err_type;    /**< Latest error type @ref blesc_error_t */
} bleam_service_health_general_data_t;

/** @brief Health error info struct
 */
typedef struct __attribute((packed)) {
    uint8_t  msg_type;                            /**< Flag that signifies this is a detailed error info message. Always should be 0x02 */
    uint32_t err_code;                            /**< The error code representing the error that occurred (from nRF SDK). */
    uint16_t line_num;                            /**< The line number where the error occurred. */
    uint8_t  file_name[BLESC_ERR_FILE_NAME_SIZE]; /**< The file in which the error occurred (first 13 symbols) */
} bleam_service_health_error_info_t;

#define BLEAM_MAX_RSSI_PER_MSG   (BLEAM_MAX_DATA_LEN / sizeof(bleam_service_rssi_data_t))   /**< Maximum amount of RSSI entries in a single message to BLEAM */

/**@brief Function for initialising parameters for and starting sending signature to BLEAM.
 *
 * @param[in] p_bleam_service_client     Pointer to the struct of BLEAM service.
 * @param[in] p_signature                Pointer to the array with signature to send over to BLEAM.
 *
 * @returns Nothing.
 */
void bleam_send_init(bleam_service_client_t *p_bleam_service_client, uint8_t *p_signature);

/**@brief Function for initialising parameters for and sending salt to BLEAM.
 *
 * @param[in] p_bleam_service_client     Pointer to the struct of BLEAM service.
 * @param[in] p_salt                     Pointer to the array with salt to send over to BLEAM.
 *
 * @returns Nothing.
 */
void bleam_send_salt(bleam_service_client_t *p_bleam_service_client, uint8_t *p_salt);

/**@brief Function for deinitialising parameters for sending data to BLEAM.
 *
 * @returns Nothing.
 */
void bleam_send_uninit(void);

/**@brief Function for continuing with assembling and sending data
 * after previous send is confirmed to be over.
 *
 * @returns Nothing.
 */
void bleam_send_continue(void);

/**@brief Function for initialising parameters for and sending salt to BLEAM.
 *
 * @param[in] sender_id     Node ID of this BLEAM Scanner.
 * @param[in] rssi          Received Signal Strength of BLEAM.
 * @param[in] aoa           Angle of arrival of BLEAM signal.
 *
 * @returns Nothing.
 */
void bleam_rssi_queue_add(uint16_t sender_id, int8_t rssi, uint8_t aoa);

/**@brief Function for initialising parameters for and sending salt to BLEAM.
 *
 * @param[in] battery_lvl     Battery level in centivolts.
 * @param[in] uptime          BLEAM Scanner node uptime.
 * @param[in] system_time     BLEAM Scanner system time.
 *
 * @returns Nothing.
 */
void bleam_health_queue_add(uint8_t battery_lvl, uint32_t uptime, uint32_t system_time);

#endif // BLEAM_SEND_HELPER_H__

/** @}*/