/* Copyright (c) 2012 Nordic Semiconductor. All Rights Reserved.
 *
 * The information contained herein is property of Nordic Semiconductor ASA.
 * Terms and conditions of usage are described in detail in NORDIC
 * SEMICONDUCTOR STANDARD SOFTWARE LICENSE AGREEMENT.
 *
 * Licensees are granted free, non-transferable use of the information. NO
 * WARRANTY of ANY KIND is provided. This heading must NOT be removed from
 * the file.
 *
 */

/* "DROPLETTER" firmware
 * Created by Ryo Hajika (imaginaryShort)
 * on Oct. 12th 2015
 * customized for the BVMCN5102/5103 chip that is using nRF51822
 */

/** @file
 *
 * @defgroup bleUartTest main.c
 * @{
 * @ingroup  bleUartTest
 * @brief    UART over BLE application main file.
 *
 * This file contains the source code for a sample application that uses the Nordic UART service.
 * This application uses the @ref srvlib_conn_params module.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "nordic_common.h"
#include "nrf.h"
#include "nrf51_bitfields.h"
#include "ble_hci.h"
#include "ble_advdata.h"
#include "ble_conn_params.h"
#include "softdevice_handler.h"
#include "app_timer.h"
#include "ble_nus.h"
#include "ble_error_log.h"
#include "ble_debug_assert_handler.h"
#include "app_util_platform.h"

#include "nrf_pwm.h"
#include "nrf_gpio.h"
#include "nrf_delay.h"

#define IS_SRVC_CHANGED_CHARACT_PRESENT 0                                           /**< Include or not the service_changed characteristic. if not enabled, the server's database cannot be changed for the lifetime of the device*/

#define DEVICE_NAME                     "DROPLETTER"                                /**< Name of device. Will be included in the advertising data. */

#define APP_ADV_INTERVAL                64                                          /**< The advertising interval (in units of 0.625 ms. This value corresponds to 40 ms). */
#define APP_ADV_TIMEOUT_IN_SECONDS      180                                         /**< The advertising timeout (in units of seconds). */

#define APP_TIMER_PRESCALER             0                                           /**< Value of the RTC1 PRESCALER register. */
#define APP_TIMER_MAX_TIMERS            4                                           /**< Maximum number of simultaneously created timers. */
#define APP_TIMER_OP_QUEUE_SIZE         4                                           /**< Size of timer operation queues. */

#define MIN_CONN_INTERVAL               16                                          /**< Minimum acceptable connection interval (20 ms), Connection interval uses 1.25 ms units. */
#define MAX_CONN_INTERVAL               60                                          /**< Maximum acceptable connection interval (75 ms), Connection interval uses 1.25 ms units. */
#define SLAVE_LATENCY                   0                                           /**< slave latency. */
#define CONN_SUP_TIMEOUT                400                                         /**< Connection supervisory timeout (4 seconds), Supervision Timeout uses 10 ms units. */
#define FIRST_CONN_PARAMS_UPDATE_DELAY  APP_TIMER_TICKS(5000, APP_TIMER_PRESCALER)  /**< Time from initiating event (connect or start of notification) to first time sd_ble_gap_conn_param_update is called (5 seconds). */
#define NEXT_CONN_PARAMS_UPDATE_DELAY   APP_TIMER_TICKS(30000, APP_TIMER_PRESCALER) /**< Time between each call to sd_ble_gap_conn_param_update after the first call (30 seconds). */
#define MAX_CONN_PARAMS_UPDATE_COUNT    3                                           /**< Number of attempts before giving up the connection parameter negotiation. */

#define SEC_PARAM_TIMEOUT               30                                          /**< Timeout for Pairing Request or Security Request (in seconds). */
#define SEC_PARAM_BOND                  1                                           /**< Perform bonding. */
#define SEC_PARAM_MITM                  0                                           /**< Man In The Middle protection not required. */
#define SEC_PARAM_IO_CAPABILITIES       BLE_GAP_IO_CAPS_NONE                        /**< No I/O capabilities. */
#define SEC_PARAM_OOB                   0                                           /**< Out Of Band data not available. */
#define SEC_PARAM_MIN_KEY_SIZE          7                                           /**< Minimum encryption key size. */
#define SEC_PARAM_MAX_KEY_SIZE          16                                          /**< Maximum encryption key size. */

#define DEAD_BEEF                       0xDEADBEEF                                  /**< Value used as error code on stack dump, can be used to identify stack location on stack unwind. */

#define TIMER_PRESCALER       (4)     /**< Prescaler setting for timers. */

static ble_gap_sec_params_t             m_sec_params;                               /**< Security requirements for this application. */
static uint16_t                         m_conn_handle = BLE_CONN_HANDLE_INVALID;    /**< Handle of the current connection. */
static ble_nus_t                        m_nus;                                      /**< Structure to identify the Nordic UART Service. */

// for this app
#define LED_PIN                         8
#define MOTOR_PIN                       9
#define ADC_SAMPLING_INTERVAL           APP_TIMER_TICKS(20, APP_TIMER_PRESCALER)   /**< Sampling rate for the ADC (10ms) */
static app_timer_id_t                   m_adc_sampling_timer_id;
uint32_t    val_rcvd_ble;
uint8_t     val_adc_result;
float       val_total_stored;
uint8_t     val_target_illuminate_pos;
uint8_t     counter_illuminate;
uint8_t     counter_motor;
bool        is_val_rcvd_ble;
bool        is_sensor_reacting;
bool        is_led_illuminating;
bool        is_motor_running;

const uint8_t led_table[]   = {255, 254, 252, 250, 247, 244, 240, 236, 232, 227,  \
                               221, 216, 213, 210, 203, 197, 190, 182, 175, 168,  \
                               160, 152, 144, 136, 128, 120, 112, 104, 96,  91,   \
                               88,  81,  74,  66,  59,  53,  46,  40,  35,  29,   \
                               24,  20,  16,  12,  9,   6,   4,   2,   1,   0};
const uint8_t motor_table[] = {0,   255, 125, 75,  0,   175, 125, 75,  0,   125, 65, 0, 65, 35, 0};


/**@brief   Function for ADC timer handler to start ADC sampling
*/
static void adc_sampling_timeout_handler(void * p_context)
{
    uint32_t    p_is_running = 0;

    sd_clock_hfclk_request();
    while(!p_is_running) {
        sd_clock_hfclk_is_running((&p_is_running));
    }
    NRF_ADC->TASKS_START = 1;
}

/**@brief Function for starting application timers.
 */
static void application_timers_start(void)
{
    uint32_t err_code;

    //ADC timer start
    err_code = app_timer_start(m_adc_sampling_timer_id, ADC_SAMPLING_INTERVAL, NULL);
    APP_ERROR_CHECK(err_code);
}

//ADC initialization
static void adc_init(void)
{   
    /* Enable interrupt on ADC sample ready event*/     
    NRF_ADC->INTENSET = ADC_INTENSET_END_Msk;   
    sd_nvic_SetPriority(ADC_IRQn, NRF_APP_PRIORITY_LOW);  
    sd_nvic_EnableIRQ(ADC_IRQn);
    
    NRF_ADC->CONFIG = (ADC_CONFIG_EXTREFSEL_None << ADC_CONFIG_EXTREFSEL_Pos)   |
                      (ADC_CONFIG_PSEL_AnalogInput2 << ADC_CONFIG_PSEL_Pos)     |
                      (ADC_CONFIG_REFSEL_VBG << ADC_CONFIG_REFSEL_Pos)          |
                      (ADC_CONFIG_INPSEL_AnalogInputOneThirdPrescaling << ADC_CONFIG_INPSEL_Pos)    |
                      (ADC_CONFIG_RES_8bit << ADC_CONFIG_RES_Pos);

    NRF_ADC->ENABLE = ADC_ENABLE_ENABLE_Enabled;
}

/* Interrupt handler for ADC data ready event */
void ADC_IRQHandler(void)
{
    /* Clear dataready event */
    NRF_ADC->EVENTS_END = 0;  

    /* send ADC result via BLE */
    val_adc_result = NRF_ADC->RESULT;
    if (val_adc_result < 75) {
        if (!is_sensor_reacting) {
            uint32_t    err_code;
            is_sensor_reacting = true;

            err_code = ble_nus_send_string(&m_nus, (uint8_t*)"PUSH", 7);
            if (err_code != NRF_ERROR_INVALID_STATE)
            {   
                APP_ERROR_CHECK(err_code);
            }
        }
    } else {
        if (is_sensor_reacting) is_sensor_reacting = false;
    }

    if (is_led_illuminating)
    {
        nrf_pwm_set_value(0, led_table[counter_illuminate]);
        if (counter_illuminate < val_target_illuminate_pos)
        {
            counter_illuminate++;
        } else {
            counter_illuminate = 0;
            is_led_illuminating = false;
        }
    }

    if (is_motor_running)
    {
        nrf_pwm_set_value(1, motor_table[counter_motor]);
        if (counter_motor < 14)
        {
            counter_motor++;
        } else {
            counter_motor = 0;
            is_motor_running = false;
        }
    }

    //Use the STOP task to save current. Workaround for PAN_028 rev1.5 anomaly 1.
    NRF_ADC->TASKS_STOP = 1;
    
    //Release the external crystal
    sd_clock_hfclk_release();
}   



/**@brief     Error handler function, which is called when an error has occurred.
 *
 * @warning   This handler is an example only and does not fit a final product. You need to analyze
 *            how your product is supposed to react in case of error.
 *
 * @param[in] error_code  Error code supplied to the handler.
 * @param[in] line_num    Line number where the handler is called.
 * @param[in] p_file_name Pointer to the file name. 
 */
void app_error_handler(uint32_t error_code, uint32_t line_num, const uint8_t * p_file_name)
{
    // This call can be used for debug purposes during application development.
    // @note CAUTION: Activating this code will write the stack to flash on an error.
    //                This function should NOT be used in a final product.
    //                It is intended STRICTLY for development/debugging purposes.
    //                The flash write will happen EVEN if the radio is active, thus interrupting
    //                any communication.
    //                Use with care. Un-comment the line below to use.
    // ble_debug_assert_handler(error_code, line_num, p_file_name);

    // On assert, the system can only recover with a reset.
    NVIC_SystemReset();
}


/**@brief       Assert macro callback function.
 *
 * @details     This function will be called in case of an assert in the SoftDevice.
 *
 * @warning     This handler is an example only and does not fit a final product. You need to
 *              analyze how your product is supposed to react in case of Assert.
 * @warning     On assert from the SoftDevice, the system can only recover on reset.
 *
 * @param[in]   line_num   Line number of the failing ASSERT call.
 * @param[in]   file_name  File name of the failing ASSERT call.
 */
void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name)
{
    app_error_handler(DEAD_BEEF, line_num, p_file_name);
}

/**@brief   Function for Timer initialization.
 *
 * @details Initializes the timer module.
 */
static void timers_init(void)
{
    uint32_t    err_code;

    // Initialize timer module
    APP_TIMER_INIT(APP_TIMER_PRESCALER, APP_TIMER_MAX_TIMERS, APP_TIMER_OP_QUEUE_SIZE, false);

    err_code = app_timer_create(&m_adc_sampling_timer_id,
                                APP_TIMER_MODE_REPEATED,
                                adc_sampling_timeout_handler);
    APP_ERROR_CHECK(err_code);
}


/**@brief   Function for the GAP initialization.
 *
 * @details This function will setup all the necessary GAP (Generic Access Profile)
 *          parameters of the device. It also sets the permissions and appearance.
 */
static void gap_params_init(void)
{
    uint32_t                err_code;
    ble_gap_conn_params_t   gap_conn_params;
    ble_gap_conn_sec_mode_t sec_mode;

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);
    
    err_code = sd_ble_gap_device_name_set(&sec_mode,
                                          (const uint8_t *) DEVICE_NAME,
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


/**@brief   Function for the Advertising functionality initialization.
 *
 * @details Encodes the required advertising data and passes it to the stack.
 *          Also builds a structure to be passed to the stack when starting advertising.
 */
static void advertising_init(void)
{
    uint32_t      err_code;
    ble_advdata_t advdata;
    ble_advdata_t scanrsp;
    uint8_t       flags = BLE_GAP_ADV_FLAGS_LE_ONLY_LIMITED_DISC_MODE;
    
    ble_uuid_t adv_uuids[] = {{BLE_UUID_NUS_SERVICE, m_nus.uuid_type}};

    memset(&advdata, 0, sizeof(advdata));
    advdata.name_type               = BLE_ADVDATA_FULL_NAME;
    advdata.include_appearance      = false;
    advdata.flags.size              = sizeof(flags);
    advdata.flags.p_data            = &flags;

    memset(&scanrsp, 0, sizeof(scanrsp));
    scanrsp.uuids_complete.uuid_cnt = sizeof(adv_uuids) / sizeof(adv_uuids[0]);
    scanrsp.uuids_complete.p_uuids  = adv_uuids;
    
    err_code = ble_advdata_set(&advdata, &scanrsp);
    APP_ERROR_CHECK(err_code);
}


/**@brief    Function for handling the data from the Nordic UART Service.
 *
 * @details  This function will process the data received from the Nordic UART BLE Service and send
 *           it to the UART module.
 */
/**@snippet [Handling the data received over BLE] */
void nus_data_handler(ble_nus_t * p_nus, uint8_t * p_data, uint16_t length)
{
    if (p_data[0] == 'b')
    {
        counter_illuminate = val_target_illuminate_pos;
        val_target_illuminate_pos = 49;
        val_rcvd_ble = 0;
        val_total_stored = 0.0;
        is_led_illuminating = true;
        is_motor_running = true;

        return;
    }

    val_rcvd_ble = atoi((char*)p_data);
    val_total_stored += (float)val_rcvd_ble/10.0;
    
    for (val_target_illuminate_pos = 0; val_target_illuminate_pos < 49; val_target_illuminate_pos++)
    {
        if (led_table[val_target_illuminate_pos] <= val_total_stored)
        {
            break;
        }
    }
    is_led_illuminating = true;
    is_motor_running = true;

    uint32_t    err_code;
    err_code = ble_nus_send_string(&m_nus, p_data, length);
    if (err_code != NRF_ERROR_INVALID_STATE)
    {   
        APP_ERROR_CHECK(err_code);
    }
}


/**@brief Function for initializing services that will be used by the application.
 */
static void services_init(void)
{
    uint32_t         err_code;
    ble_nus_init_t   nus_init;
    
    memset(&nus_init, 0, sizeof(nus_init));

    nus_init.data_handler = nus_data_handler;
    
    err_code = ble_nus_init(&m_nus, &nus_init);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for initializing security parameters.
 */
static void sec_params_init(void)
{
    m_sec_params.timeout      = SEC_PARAM_TIMEOUT;
    m_sec_params.bond         = SEC_PARAM_BOND;
    m_sec_params.mitm         = SEC_PARAM_MITM;
    m_sec_params.io_caps      = SEC_PARAM_IO_CAPABILITIES;
    m_sec_params.oob          = SEC_PARAM_OOB;  
    m_sec_params.min_key_size = SEC_PARAM_MIN_KEY_SIZE;
    m_sec_params.max_key_size = SEC_PARAM_MAX_KEY_SIZE;
}


/**@brief       Function for handling an event from the Connection Parameters Module.
 *
 * @details     This function will be called for all events in the Connection Parameters Module
 *              which are passed to the application.
 *
 * @note        All this function does is to disconnect. This could have been done by simply setting
 *              the disconnect_on_fail config parameter, but instead we use the event handler
 *              mechanism to demonstrate its use.
 *
 * @param[in]   p_evt   Event received from the Connection Parameters Module.
 */
static void on_conn_params_evt(ble_conn_params_evt_t * p_evt)
{
    uint32_t err_code;
    
    if(p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED)
    {
        err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
        APP_ERROR_CHECK(err_code);
    }
}


/**@brief       Function for handling errors from the Connection Parameters module.
 *
 * @param[in]   nrf_error   Error code containing information about what went wrong.
 */
static void conn_params_error_handler(uint32_t nrf_error)
{
    APP_ERROR_HANDLER(nrf_error);
}


/**@brief Function for initializing the Connection Parameters module.
 */
static void conn_params_init(void)
{
    uint32_t               err_code;
    ble_conn_params_init_t cp_init;
    
    memset(&cp_init, 0, sizeof(cp_init));

    cp_init.p_conn_params                  = NULL;
    cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
    cp_init.next_conn_params_update_delay  = NEXT_CONN_PARAMS_UPDATE_DELAY;
    cp_init.max_conn_params_update_count   = MAX_CONN_PARAMS_UPDATE_COUNT;
    cp_init.start_on_notify_cccd_handle    = BLE_GATT_HANDLE_INVALID;
    cp_init.disconnect_on_fail             = false;
    cp_init.evt_handler                    = on_conn_params_evt;
    cp_init.error_handler                  = conn_params_error_handler;
    
    err_code = ble_conn_params_init(&cp_init);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for starting advertising.
 */
static void advertising_start(void)
{
    uint32_t             err_code;
    ble_gap_adv_params_t adv_params;
    
    // Start advertising
    memset(&adv_params, 0, sizeof(adv_params));
    
    adv_params.type        = BLE_GAP_ADV_TYPE_ADV_IND;
    adv_params.p_peer_addr = NULL;
    adv_params.fp          = BLE_GAP_ADV_FP_ANY;
    adv_params.interval    = APP_ADV_INTERVAL;
    adv_params.timeout     = APP_ADV_TIMEOUT_IN_SECONDS;

    err_code = sd_ble_gap_adv_start(&adv_params);
    APP_ERROR_CHECK(err_code);
}


/**@brief       Function for the Application's S110 SoftDevice event handler.
 *
 * @param[in]   p_ble_evt   S110 SoftDevice event.
 */
static void on_ble_evt(ble_evt_t * p_ble_evt)
{
    uint32_t                         err_code;
    static ble_gap_evt_auth_status_t m_auth_status;
    ble_gap_enc_info_t *             p_enc_info;
    
    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GAP_EVT_CONNECTED:
            m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
            break;
            
        case BLE_GAP_EVT_DISCONNECTED:
            m_conn_handle = BLE_CONN_HANDLE_INVALID;
            advertising_start();

            break;
            
        case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
            err_code = sd_ble_gap_sec_params_reply(m_conn_handle, 
                                                   BLE_GAP_SEC_STATUS_SUCCESS, 
                                                   &m_sec_params);
            APP_ERROR_CHECK(err_code);
            break;
            
        case BLE_GATTS_EVT_SYS_ATTR_MISSING:
            err_code = sd_ble_gatts_sys_attr_set(m_conn_handle, NULL, 0);
            APP_ERROR_CHECK(err_code);
            break;

        case BLE_GAP_EVT_AUTH_STATUS:
            m_auth_status = p_ble_evt->evt.gap_evt.params.auth_status;
            break;
            
        case BLE_GAP_EVT_SEC_INFO_REQUEST:
            p_enc_info = &m_auth_status.periph_keys.enc_info;
            if (p_enc_info->div == p_ble_evt->evt.gap_evt.params.sec_info_request.div)
            {
                err_code = sd_ble_gap_sec_info_reply(m_conn_handle, p_enc_info, NULL);
                APP_ERROR_CHECK(err_code);
            }
            else
            {
                // No keys found for this device
                err_code = sd_ble_gap_sec_info_reply(m_conn_handle, NULL, NULL);
                APP_ERROR_CHECK(err_code);
            }
            break;
/*
        case BLE_GAP_EVT_TIMEOUT:
            if (p_ble_evt->evt.gap_evt.params.timeout.src == BLE_GAP_TIMEOUT_SRC_ADVERTISEMENT)
            {   
                // Go to system-off mode (this function will not return; wakeup will cause a reset)
                err_code = sd_power_system_off();    
                APP_ERROR_CHECK(err_code);
            }
            break;
*/
        default:
            // No implementation needed.
            break;
    }
}


/**@brief       Function for dispatching a S110 SoftDevice event to all modules with a S110
 *              SoftDevice event handler.
 *
 * @details     This function is called from the S110 SoftDevice event interrupt handler after a
 *              S110 SoftDevice event has been received.
 *
 * @param[in]   p_ble_evt   S110 SoftDevice event.
 */
static void ble_evt_dispatch(ble_evt_t * p_ble_evt)
{
    ble_conn_params_on_ble_evt(p_ble_evt);
    ble_nus_on_ble_evt(&m_nus, p_ble_evt);
    on_ble_evt(p_ble_evt);
}


/**@brief   Function for the S110 SoftDevice initialization.
 *
 * @details This function initializes the S110 SoftDevice and the BLE event interrupt.
 */
static void ble_stack_init(void)
{
    uint32_t err_code;
    
    // Initialize SoftDevice.
    SOFTDEVICE_HANDLER_INIT(NRF_CLOCK_LFCLKSRC_RC_250_PPM_4000MS_CALIBRATION, false);

    // Enable BLE stack 
    ble_enable_params_t ble_enable_params;
    memset(&ble_enable_params, 0, sizeof(ble_enable_params));
    ble_enable_params.gatts_enable_params.service_changed = IS_SRVC_CHANGED_CHARACT_PRESENT;
    err_code = sd_ble_enable(&ble_enable_params);
    APP_ERROR_CHECK(err_code);
    
    // Subscribe for BLE events.
    err_code = softdevice_ble_evt_handler_set(ble_evt_dispatch);
    APP_ERROR_CHECK(err_code);
}


/**@brief  Function for placing the application in low power state while waiting for events.
 */
static void power_manage(void)
{
    uint32_t err_code = sd_app_evt_wait();
    APP_ERROR_CHECK(err_code);
}


/**@brief  Application main function.
 */
int main(void)
{
    // Initialize
    val_rcvd_ble = 0;
    val_adc_result = 0;
    val_total_stored = 0;
    val_target_illuminate_pos = 0;
    counter_illuminate = 0;
    is_val_rcvd_ble = false;
    is_sensor_reacting = false;
    is_led_illuminating = false;
    counter_motor = 0;
    is_motor_running = false;

    timers_init();
    ble_stack_init();
    gap_params_init();
    services_init();
    advertising_init();
    conn_params_init();
    sec_params_init();

    adc_init();

    nrf_pwm_config_t pwm_config = PWM_DEFAULT_CONFIG;
    pwm_config.mode = PWM_MODE_LED_255;
    pwm_config.num_channels = 2;
    pwm_config.gpio_num[0] = LED_PIN;
    pwm_config.gpio_num[1] = MOTOR_PIN;
    nrf_pwm_init(&pwm_config);
    nrf_pwm_set_value(0, 0);
    nrf_pwm_set_value(1, 0);
    
    application_timers_start();
    advertising_start();
    
    // Enter main loop
    for (;;)
    {
        power_manage();
    }
}

/** 
 * @}
 */
