/** @file blesc_error.c
 *
 * @addtogroup blesc_error BLEAM Scanner custom error handler
 * @{
 */

#include "app_error.h"
#include "blesc_error.h"
#include "app_config.h"

#include "log.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "app_util_platform.h"
#include "nrf_strerror.h"

#if defined(SOFTDEVICE_PRESENT) && SOFTDEVICE_PRESENT
#include "nrf_sdm.h"
#endif

/** @brief Retained error info variable.
 *
 * This variable is created in protected RAM section and will retain its value after soft reset.
 */
blesc_retained_error_t blesc_error __attribute__((section(".retained_section")));

/** Buffer for random error ID generation. */
static uint8_t m_rng_buff[2] = {0xDE, 0xAD};

/**@brief Function for saving error data to retained BLEAM Scanner error variable
 *
 *
 * @param[in] error_type   Type of error as in @ref blesc_error_t.
 * @param[in] line_num     Pointer to the line number where the error occurred.
 * @param[in] filepath     Pointer to the file in which the error occurred (first 13 symbols).
 * @param[in] p_err_code   Pointer to the error code representing the error that occurred (from nRF SDK).
 */
static void blesc_error_save(blesc_error_t error_type,
                             uint16_t *line_num,
                             uint8_t const *filepath,
                             uint32_t *p_err_code) {
    blesc_error.error_type = error_type;
    memcpy(&blesc_error.random_id, m_rng_buff, 2);

    if (NULL != line_num && NULL != filepath) {
        blesc_error.error_info.line_num = *line_num;

        uint8_t *file_name;

        // parse filepath for file name
        uint8_t *slash = (uint8_t *)filepath, *next;
        while ((next = strpbrk(slash + 1, "\\/")))
            slash = next;
        if (filepath != slash)
            slash++;
        file_name = strdup(slash);

        size_t file_name_size = strlen(file_name);
        if (file_name_size > BLESC_ERR_FILE_NAME_SIZE)
            file_name_size = BLESC_ERR_FILE_NAME_SIZE;
        memcpy(blesc_error.error_info.file_name, file_name, file_name_size);
    } else {
        blesc_error.error_info.line_num = 0;
        memset(blesc_error.error_info.file_name, 0, BLESC_ERR_FILE_NAME_SIZE);
    }

    if(NULL != p_err_code) {
        blesc_error.error_info.err_code = *p_err_code;
    } else {
        blesc_error.error_info.err_code = 0;
    }
}

/**@brief Function for printing retained error info
 *
 */
static void blesc_error_log(void) {
    switch (blesc_error.error_type) {
    case BLESC_ERR_T_HARD_RESET:
        __LOG(LOG_SRC_APP, LOG_LEVEL_REPORT, "No errors since hard reset!\r\n");
        return;

    case BLESC_ERR_T_SOFT_RESET:
        __LOG(LOG_SRC_APP, LOG_LEVEL_REPORT, "Planned soft resets only!\r\n");
        return;

    case BLESC_ERR_T_SD_ASSERT:
    case BLESC_ERR_T_APP_MEMACC:
        __LOG(LOG_SRC_APP, LOG_LEVEL_REPORT, "Previous error: %s\r\n",
                    (blesc_error.error_type == BLESC_ERR_T_SD_ASSERT) ? "SOFTDEVICE ASSERT" : "MEMORY ACCESS ERROR");
        return;

    case BLESC_ERR_T_SDK_ASSERT:
        __LOG(LOG_SRC_APP, LOG_LEVEL_REPORT, "Previous error: SDK ASSERT\r\n");
        break;

    case BLESC_ERR_T_SDK_ERROR:
        __LOG(LOG_SRC_APP, LOG_LEVEL_REPORT, "Previous error: SDK ERROR 0x%08x (%s)\r\n",
                    blesc_error.error_info.err_code,
                    nrf_strerror_get(blesc_error.error_info.err_code)
                    );
        break;

    default:
        __LOG(LOG_SRC_APP, LOG_LEVEL_REPORT, "Previous error: UNKNOWN ERROR 0x%08x (%s)\r\n",
                    blesc_error.error_info.err_code,
                    nrf_strerror_get(blesc_error.error_info.err_code)
                    );
        break;
    }

    if(blesc_error.error_info.line_num > 0)
        __LOG(LOG_SRC_APP, LOG_LEVEL_REPORT, "Location: %u:%s\r\n", blesc_error.error_info.line_num, (uint32_t)blesc_error.error_info.file_name);

}

void blesc_error_on_boot(void) {
    // If no error data retained, set it to hard reset
    ret_code_t err_code = NRF_SUCCESS;
    err_code = sd_rand_application_vector_get(m_rng_buff, 2);
    APP_ERROR_CHECK(err_code);
    uint32_t gpregret_flag = 0;
    sd_power_gpregret_get(0, &gpregret_flag);
    if(gpregret_flag != BLESC_GPREGRET_RETAINED_VALUE) {
        sd_power_gpregret_clr(0, 0xFF);
        sd_power_gpregret_set(0, BLESC_GPREGRET_RETAINED_VALUE);
        memset(&blesc_error, 0, sizeof(blesc_retained_error_t));
        memcpy(&blesc_error.random_id, m_rng_buff, 2);
    } else if(BLESC_ERR_T_HARD_RESET == blesc_error.error_type) {
        blesc_error.error_type = BLESC_ERR_T_SOFT_RESET;
    }
    blesc_error_log();
}

blesc_retained_error_t blesc_error_get(void) {
    return blesc_error;
}

/**@brief Redefinition of __WEAK app_error_fault_handler from app_error_weak.c */
void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info)
{
    __disable_irq();
    NRF_LOG_FINAL_FLUSH();

    switch (id)
    {
#if defined(SOFTDEVICE_PRESENT) && SOFTDEVICE_PRESENT
        case NRF_FAULT_ID_SD_ASSERT:
            NRF_LOG_ERROR("SOFTDEVICE: ASSERTION FAILED");
            blesc_error_save(BLESC_ERR_T_SD_ASSERT, NULL, NULL, NULL);
            break;
        case NRF_FAULT_ID_APP_MEMACC:
            NRF_LOG_ERROR("SOFTDEVICE: INVALID MEMORY ACCESS");
            blesc_error_save(BLESC_ERR_T_APP_MEMACC, NULL, NULL, NULL);
            break;
#endif
        case NRF_FAULT_ID_SDK_ASSERT:
        {
            assert_info_t * p_info = (assert_info_t *)info;
            NRF_LOG_ERROR("ASSERTION FAILED at %s:%u",
                          p_info->p_file_name,
                          p_info->line_num);
            blesc_error_save(BLESC_ERR_T_SDK_ASSERT, &p_info->line_num, p_info->p_file_name, NULL);
            break;
        }
        case NRF_FAULT_ID_SDK_ERROR:
        {
            error_info_t * p_info = (error_info_t *)info;
            NRF_LOG_ERROR("ERROR %u [%s] at %s:%u\r\nPC at: 0x%08x",
                          p_info->err_code,
                          nrf_strerror_get(p_info->err_code),
                          p_info->p_file_name,
                          p_info->line_num,
                          pc);
            blesc_error_save(BLESC_ERR_T_SDK_ERROR, (uint16_t *)&p_info->line_num, p_info->p_file_name, &p_info->err_code);
            break;
        }
        default:
            NRF_LOG_ERROR("UNKNOWN FAULT at 0x%08X", pc);
            blesc_error_save(BLESC_ERR_T_UNKNOWN, NULL, NULL, NULL);
            break;
    }

//    NRF_BREAKPOINT_COND;
    // On assert, the system can only recover with a reset.

    NRF_LOG_WARNING("System reset");
    NVIC_SystemReset();
}
/*lint -restore */

/** @}*/