#include "joysticks.h"
#include "esp_err.h"
#include "wifi.h"

const char *TAG = "joysticks";
static virtual_buttons_t virtual_buttons = {0};

static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle) {
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}

void task_joysticks(void *args) {
    esp_err_t ret;
    // config STICK_X_PIN & STICK_Y_PIN as ADC input
    ESP_LOGI(TAG, "Configuring ADC pin X:%d & Y:%d", STICK_X_PIN, STICK_Y_PIN);
    adc_oneshot_unit_handle_t adc2_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_2,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ret = adc_oneshot_new_unit(&init_config1, &adc2_handle);
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = SOC_ADC_RTC_MAX_BITWIDTH,
        .atten = ADC_ATTEN_DB_11,
    };
    adc_oneshot_config_channel(adc2_handle, STICK_X_CHAN, &config);
    adc_oneshot_config_channel(adc2_handle, STICK_Y_CHAN, &config);
    
    adc_cali_handle_t adc2_cali_chan0_handle = NULL;
    adc_cali_handle_t adc2_cali_chan1_handle = NULL;
    bool do_calibration1_chan0 = adc_calibration_init(ADC_UNIT_2, STICK_X_CHAN, STICK_ATTEN, &adc2_cali_chan0_handle);
    bool do_calibration1_chan1 = adc_calibration_init(ADC_UNIT_2, STICK_Y_CHAN, STICK_ATTEN, &adc2_cali_chan1_handle);

    // range: [-1.0, 1.0]
    float x = 0, y = 0;
    // float x_last = 0, y_last = 0;
    virtual_buttons_t virtual_buttons_last = {0};
    int adc_raw[2];
    int voltage[2];
    ret = ESP_OK;
    while (true) {
        // if retry from timeout, wait more time
        // ADC2 cannot use while WIFI is up
        if (wifi_is_started()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (ret == ESP_ERR_TIMEOUT) {
            vTaskDelay(pdMS_TO_TICKS(1500));
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        ret = adc_oneshot_read(adc2_handle, STICK_X_CHAN, &adc_raw[0]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ADC%d Channel[%d] Error: %d", ADC_UNIT_2 + 1, STICK_X_CHAN, ret);
            continue;
        }
        ESP_LOGD(TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_2 + 1, STICK_X_CHAN, adc_raw[0]);
        if (do_calibration1_chan0) {
            ret = adc_cali_raw_to_voltage(adc2_cali_chan0_handle, adc_raw[0], &voltage[0]);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "ADC%d Channel[%d] Cali Error: %d", ADC_UNIT_2 + 1, STICK_X_CHAN, ret);
                continue;
            }
            ESP_LOGD(TAG, "ADC%d Channel[%d] Cali Voltage: %d mV", ADC_UNIT_2 + 1, STICK_X_CHAN, voltage[0]);
        }
        // vTaskDelay(pdMS_TO_TICKS(1000));

        ret = adc_oneshot_read(adc2_handle, STICK_Y_CHAN, &adc_raw[1]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ADC%d Channel[%d] Error: %d", ADC_UNIT_2 + 1, STICK_Y_CHAN, ret);
            continue;
        }
        ESP_LOGD(TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_2 + 1, STICK_Y_CHAN, adc_raw[1]);
        if (do_calibration1_chan1) {
            ret = adc_cali_raw_to_voltage(adc2_cali_chan1_handle, adc_raw[1], &voltage[1]);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "ADC%d Channel[%d] Cali Error: %d", ADC_UNIT_2 + 1, STICK_Y_CHAN, ret);
                continue;
            }
            ESP_LOGD(TAG, "ADC%d Channel[%d] Cali Voltage: %d mV", ADC_UNIT_2 + 1, STICK_Y_CHAN, voltage[1]);
        }
        x = (float)(voltage[0] + STICK_X_OFFSET - STICK_X_MIN) / (STICK_X_MAX - STICK_X_MIN) * 2 - 1;
        y = -((float)(voltage[1] + STICK_Y_OFFSET - STICK_Y_MIN) / (STICK_Y_MAX - STICK_Y_MIN) * 2 - 1);
        ESP_LOGI(TAG, "x: %.03f, y: %.03f; vx: %04d, vy: %04d, temp: %.03f", x, y, voltage[0], voltage[1], epd_ambient_temperature());
        if (fabs(x) < STICK_X_DEAD_ZONE) x = 0.0f;
        if (fabs(y) < STICK_Y_DEAD_ZONE) y = 0.0f;

        if (x >= STICK_X_THRESHOLD_HIGH) {
            virtual_buttons.right = true;
        } else if (x <= STICK_X_THRESHOLD_LOW) {
            virtual_buttons.left = true;
        } else {
            virtual_buttons.right = false;
            virtual_buttons.left = false;
        }
        if (y >= STICK_Y_THRESHOLD_HIGH) {
            virtual_buttons.up = true;
        } else if (y <= STICK_Y_THRESHOLD_LOW) {
            virtual_buttons.down = true;
        } else {
            virtual_buttons.up = false;
            virtual_buttons.down = false;
        }

        // x_last = x;
        // y_last = y;
        memcpy(&virtual_buttons_last, &virtual_buttons, sizeof(virtual_buttons_t));
    }
    // for safety exit
    // vTaskDelete(NULL);
}