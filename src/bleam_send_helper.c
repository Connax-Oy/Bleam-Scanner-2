/** @file bleam_send_helper.c
 *
 * @defgroup bleam_send_helper BLEAM send helper
 * @{
 * @ingroup bleam_service
 *
 * @brief Packaging data for sending to BLEAM.
 */

#include "bleam_send_helper.h"
#include "nrf_crypto.h"
#include "log.h"

/** RSSI data queue for BLEAM */
bleam_service_rssi_data_t bleam_rssi_queue[BLEAM_QUEUE_SIZE];
uint16_t bleam_rssi_queue_front; /**< Index of the front element of the RSSI data queue */
uint16_t bleam_rssi_queue_back;  /**< Index of the back element of the RSSI data queue */

/* Health data for BLEAM */
bleam_service_health_general_data_t health_general_message; /**< General health status data message struct. */
bleam_service_health_error_info_t   health_error_info;      /**< Detailed error info message struct. */

bleam_service_client_t *m_bleam_service_client; /**< Pointer to BLEAM service client instance */
uint16_t                m_bleam_send_char;      /**< Characteristic to write to */
uint8_t                 m_data_index;           /**< Index inside data array */
uint8_t                *m_signature;            /**< Pointer to array with signature to send */

/* Forward declarations */
static void bleam_send_health(void);
static void bleam_send_rssi(void);

/**@brief Function for writing data to BLEAM.
 *
 * @details This function sends the contents of p_data_array[]
 *          of size p_data_len over to bleam_service to be sent to BLEAM.
 *          Function should only be called after connection to BLEAM is established
 *          and the @ref m_bleam_send_char characteristic is discovered.
 *
 * @returns Nothing.
 */
static void bleam_send_write_data(uint8_t * p_data_array, uint16_t p_data_len) {
    ret_code_t err_code = NRF_SUCCESS;
    if (0 == p_data_len) {
        return;
    }
    err_code = bleam_service_data_send(m_bleam_service_client, p_data_array, &p_data_len, m_bleam_send_char);
    if (err_code != NRF_ERROR_INVALID_STATE) {
        APP_ERROR_CHECK(err_code);
    }
}

/**************************** SEND SIGNATURE *****************************/

/**@brief Function for fragmenting signature to send to BLEAM.
 *
 * @details Function for sending signature hash over to BLEAM.
 *          It takes data from signature array @ref m_signature[]
 *          and packs it into an array.
 *          When the array is full, it calls @ref bleam_send_write_data() to write this data.
 *
 * @returns Nothing.
 */
static void bleam_send_signature(void) {
    if(NRF_CRYPTO_HASH_SIZE_SHA256 <= m_data_index) {
        m_bleam_send_char = 0;
//        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "BLEAM signature send DONE\r\n");

        bleam_service_client_evt_t evt;
        evt.evt_type = BLEAM_SERVICE_CLIENT_EVT_DONE_SENDING_SIGNATURE;
        m_bleam_service_client->evt_handler(m_bleam_service_client, &evt);
        return;
    }

    uint16_t data_size = BLEAM_MAX_DATA_LEN;
    uint8_t data_array[BLEAM_MAX_DATA_LEN] = {0};

    if(NRF_CRYPTO_HASH_SIZE_SHA256 > m_data_index + BLEAM_MAX_DATA_LEN)
        data_size = BLEAM_MAX_DATA_LEN;
    else
        data_size = NRF_CRYPTO_HASH_SIZE_SHA256 - m_data_index;
    memcpy(data_array, m_signature + m_data_index, data_size);
    m_data_index = m_data_index + BLEAM_MAX_DATA_LEN;

    bleam_send_write_data(data_array, data_size);
}

/**@brief Function for sending salt to BLEAM via signature char.
 *
 * @details Function for sending salt over to BLEAM.
 *          It takes data from salt array @ref m_signature[]
 *          and packs it into an array.
 *          When the array is full, it calls @ref bleam_send_write_data() to write this data.
 *
 * @returns Nothing.
 */
static void bleam_send_salt_as_signature(void) {
    uint16_t data_size = BLEAM_MAX_DATA_LEN;
    bleam_send_write_data(m_signature, data_size);
}

/****************************** SEND HEALTH ******************************/

/**@brief Function for assembling health data to send to BLEAM.
 *
 * @returns Nothing.
 */
 static void bleam_send_health(void) {
    if(0 == health_general_message.msg_type && 0 == health_error_info.msg_type) {
        bleam_send_rssi();
        return;
    }

    uint8_t data_array[BLEAM_MAX_DATA_LEN] = {0};
    uint16_t msg_len = 0;

    if(0 != health_general_message.msg_type) {
        msg_len = sizeof(bleam_service_health_general_data_t);
        memcpy(data_array, (uint8_t *)(&health_general_message), msg_len);
        memset(&health_general_message, 0, msg_len);
    } else if (0 != health_error_info.msg_type) {
        msg_len = sizeof(bleam_service_health_error_info_t);
        memcpy(data_array, (uint8_t *)(&health_error_info), msg_len);
        memset(&health_error_info, 0, msg_len);
    }

    m_bleam_send_char = BLEAM_S_HEALTH;
    bleam_send_write_data(data_array, msg_len);
}

/******************************* SEND RSSI *******************************/

/**@brief Function for assembling RSSI data to send to BLEAM.
 *
 * @returns Nothing.
 */
static void bleam_send_rssi(void) {
    uint8_t rssi_in_msg = BLEAM_MAX_RSSI_PER_MSG;
    if(bleam_rssi_queue_back == bleam_rssi_queue_front) {
        bleam_rssi_queue_back = bleam_rssi_queue_front = 0;
        m_bleam_send_char = 0;

        bleam_service_client_evt_t evt;
        evt.evt_type = BLEAM_SERVICE_CLIENT_EVT_DONE_SENDING;
        m_bleam_service_client->evt_handler(m_bleam_service_client, &evt);
        return;
    } else if(bleam_rssi_queue_back > bleam_rssi_queue_front &&
            bleam_rssi_queue_back - bleam_rssi_queue_front < BLEAM_MAX_RSSI_PER_MSG) {
        rssi_in_msg = bleam_rssi_queue_back - bleam_rssi_queue_front;
    } else if(bleam_rssi_queue_back < bleam_rssi_queue_front &&
            BLEAM_QUEUE_SIZE - bleam_rssi_queue_front < BLEAM_MAX_RSSI_PER_MSG) {
        rssi_in_msg = BLEAM_QUEUE_SIZE - bleam_rssi_queue_front;
    }
    
    uint16_t msg_len = rssi_in_msg * sizeof(bleam_service_rssi_data_t);
    uint8_t data_array[BLEAM_MAX_DATA_LEN] = {0};
    memcpy(data_array, (uint8_t *)(bleam_rssi_queue + bleam_rssi_queue_front), msg_len);

    m_bleam_send_char = BLEAM_S_RSSI;
    bleam_send_write_data(data_array, msg_len);
    bleam_rssi_queue_front += rssi_in_msg;
    bleam_rssi_queue_front %= BLEAM_QUEUE_SIZE;
}

/********************************** INTERFACE *********************************/

void bleam_send_init(bleam_service_client_t *p_bleam_service_client, uint8_t *p_signature) {
    m_bleam_service_client   = p_bleam_service_client;
    m_data_index             = 0;
    m_signature              = p_signature;
    m_bleam_send_char        = BLEAM_S_SIGN;
    bleam_send_signature();
}

void bleam_send_salt(bleam_service_client_t *p_bleam_service_client, uint8_t *p_salt) {
    m_bleam_service_client = p_bleam_service_client;
    m_signature            = p_salt;
    m_bleam_send_char      = BLEAM_S_SIGN;
    bleam_send_salt_as_signature();
}

void bleam_send_uninit(void) {
    m_bleam_service_client   = NULL;
    m_signature              = NULL;
    m_bleam_send_char        = 0;
    bleam_rssi_queue_front   = 0;
    bleam_rssi_queue_back    = 0;
}

void bleam_send_continue(void) {
    // If sending signature, finish with signature.
    // Otherwise send all the health first, then RSSI
    switch (m_bleam_send_char) {
    case BLEAM_S_SIGN:
        bleam_send_signature(); 
        break;
    case 0:
        m_bleam_send_char = BLEAM_S_HEALTH;
    default:
        bleam_send_health();
        break;
    }
}

void bleam_rssi_queue_add(uint16_t sender_id, int8_t rssi, uint8_t aoa) {
    bleam_rssi_queue[bleam_rssi_queue_back].sender_id = sender_id;
    bleam_rssi_queue[bleam_rssi_queue_back].rssi = rssi;
    bleam_rssi_queue[bleam_rssi_queue_back].aoa = aoa;
    bleam_rssi_queue_back = (bleam_rssi_queue_back + 1) % BLEAM_QUEUE_SIZE;
    if(bleam_rssi_queue_back == bleam_rssi_queue_front)
        bleam_rssi_queue_front = (bleam_rssi_queue_front + 1) % BLEAM_QUEUE_SIZE;
    if(0 == m_bleam_send_char && NULL != m_bleam_service_client) {
        bleam_send_continue();
    }
}

void bleam_health_queue_add(uint8_t battery_lvl, uint32_t uptime, uint32_t system_time) {
    health_general_message.msg_type    = 0x01;
    health_general_message.battery_lvl = battery_lvl;
    health_general_message.fw_id       = APP_CONFIG_FW_VERSION_ID;
    health_general_message.uptime      = uptime;
    health_general_message.system_time = system_time;

    blesc_retained_error_t blesc_error = blesc_error_get();

    health_general_message.err_id      = blesc_error.random_id;
    health_general_message.err_type    = blesc_error.error_type;

    // Only these error types require a detailed error message
    if(BLESC_ERR_T_SDK_ASSERT == blesc_error.error_type || BLESC_ERR_T_SDK_ERROR == blesc_error.error_type) {
        health_error_info.msg_type = 0x02;
        health_error_info.err_code = blesc_error.error_info.err_code;
        health_error_info.line_num = blesc_error.error_info.line_num;
        memcpy(health_error_info.file_name, blesc_error.error_info.file_name, BLESC_ERR_FILE_NAME_SIZE);
    } else {
        health_error_info.msg_type = 0x00;
    }

    if(0 == m_bleam_send_char && NULL != m_bleam_service_client) {
        bleam_send_continue();
    }
}

/** @}*/