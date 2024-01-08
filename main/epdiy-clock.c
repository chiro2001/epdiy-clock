#include "common.h"

/// global variables

EpdiyHighlevelState hl;

// buffers
uint8_t* source_buf = NULL;       // downloaded image
static uint8_t tjpgd_work[3096];  // tjpgd 3096 is the minimum size
uint8_t* fb;                      // EPD 2bpp buffer

// Load the EMBED_TXTFILES. Then doing (char*) server_cert_pem_start you get the SSL certificate
// Reference:
// https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html#embedding-binary-data
extern const uint8_t server_cert_pem_start[] asm("_binary_server_cert_pem_start");

// JPEG decoder
JDEC jd;
JRESULT rc;

// time
int64_t time_download_start;
int64_t time_download;
int64_t time_decomp;
int64_t time_render;

static const char* jd_errors[] = {
    "Succeeded",
    "Interrupted by output function",
    "Device error or wrong termination of input stream",
    "Insufficient memory pool for the image",
    "Insufficient stream input buffer",
    "Parameter error",
    "Data format error",
    "Right format but not supported",
    "Not supported JPEG standard"};

uint8_t gamme_curve[256];

void generate_gamme(double gamma_value) {
  double gammaCorrection = 1.0 / gamma_value;
  for (int gray_value = 0; gray_value < 256; gray_value++)
    gamme_curve[gray_value] = round(255 * pow(gray_value / 255.0, gammaCorrection));
}

#if VALIDATE_SSL_CERTIFICATE
/* Time aware for ESP32: Important to check SSL certs validity */
void time_sync_notification_cb(struct timeval* tv) {
  ESP_LOGI(TAG, "Notification of a time synchronization event");
}

static void print_sntp_servers(void) {
  ESP_LOGI(TAG, "List of configured NTP servers:");

  for (uint8_t i = 0; i < SNTP_MAX_SERVERS; ++i) {
    if (esp_sntp_getservername(i)) {
      ESP_LOGI(TAG, "server %d: %s", i, esp_sntp_getservername(i));
    } else {
      // we have either IPv4 or IPv6 address, let's print it
      char buff[INET6_ADDRSTRLEN];
      ip_addr_t const* ip = esp_sntp_getserver(i);
      if (ipaddr_ntoa_r(ip, buff, INET6_ADDRSTRLEN) != NULL)
          ESP_LOGI(TAG, "server %d: %s", i, buff);
    }
  }
}

static void initialize_sntp(void) {
    ESP_LOGI(TAG, "Initializing SNTP");
    // esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_SERVER);
    esp_sntp_config_t config = NTP_SERVER_CONFIG;
    config.sync_cb = time_sync_notification_cb;
    esp_netif_sntp_init(&config);
    print_sntp_servers();
}

static void obtain_time(void) {
    initialize_sntp();

    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 15;
    while (esp_netif_sntp_sync_wait(2000 / portTICK_PERIOD_MS) == ESP_ERR_TIMEOUT && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
    }
    if (retry == retry_count) {
        ESP_LOGE(TAG, "Failed to obtain time over SNTP");
    }
    time(&now);
    localtime_r(&now, &timeinfo);
    ESP_LOGI(TAG, "Current time: %s", asctime(&timeinfo));
}
#endif

/* User defined call-back function to output decoded RGB bitmap in decoded_image buffer */
static uint32_t tjd_output(
    JDEC* jd,     /* Decompressor object of current session */
    void* bitmap, /* Bitmap data to be output */
    JRECT* rect   /* Rectangular region to output */
) {
    vTaskDelay(0);

    uint32_t w = rect->right - rect->left + 1;
    uint32_t h = rect->bottom - rect->top + 1;
    uint32_t image_width = jd->width;
    uint8_t* bitmap_ptr = (uint8_t*)bitmap;

    // Write to display
    uint32_t padding_x = (epd_rotated_display_width() - jd->width) / 2;
    uint32_t padding_y = (epd_rotated_display_height() - jd->height) / 2;
    
    for (uint32_t i = 0; i < w * h; i++) {
        uint8_t r = *(bitmap_ptr++);
        uint8_t g = *(bitmap_ptr++);
        uint8_t b = *(bitmap_ptr++);

        // Calculate weighted grayscale
        // uint32_t val = ((r * 30 + g * 59 + b * 11) / 100); // original formula
        uint32_t val = (r * 38 + g * 75 + b * 15) >> 7;  // @vroland recommended formula

        int xx = rect->left + i % w;
        if (xx < 0 || xx >= image_width) {
            continue;
        }
        int yy = rect->top + i / w;
        if (yy < 0 || yy >= jd->height) {
            continue;
        }

        /* Optimization note: If we manage to apply here the epd_draw_pixel directly
           then it will be no need to keep a huge raw buffer (But will loose dither) */
        // decoded_image[yy * image_width + xx] = gamme_curve[val];
        epd_draw_pixel(xx + padding_x, yy + padding_y, gamme_curve[val], fb);
    }

    return 1;
}

static uint32_t feed_buffer(
    JDEC* jd,
    uint8_t* buff,  // Pointer to the read buffer (NULL:skip)
    uint32_t nd
) {
    uint32_t count = 0;
    static uint32_t buffer_pos = 0;

    while (count < nd) {
        if (buff != NULL) {
            *buff++ = source_buf[buffer_pos];
        }
        count++;
        buffer_pos++;
    }

    return count;
}

//====================================================================================
//   This function opens source_buf Jpeg image file and primes the decoder
//====================================================================================
int drawBufJpeg(uint8_t* source_buf, int xpos, int ypos) {
    rc = jd_prepare(&jd, feed_buffer, tjpgd_work, sizeof(tjpgd_work), &source_buf);
    if (rc != JDR_OK) {
        ESP_LOGE(TAG, "JPG jd_prepare error: %s", jd_errors[rc]);
        return ESP_FAIL;
    }

    uint32_t decode_start = esp_timer_get_time();

    // Last parameter scales        v 1 will reduce the image
    rc = jd_decomp(&jd, tjd_output, 0);
    if (rc != JDR_OK) {
        ESP_LOGE(TAG, "JPG jd_decomp error: %s", jd_errors[rc]);
        return ESP_FAIL;
    }

    time_decomp = (esp_timer_get_time() - decode_start) / 1000;

    ESP_LOGI("JPG", "width: %d height: %d\n", jd.width, jd.height);
    ESP_LOGI("decode", "%" PRIu32 " ms . image decompression", time_decomp);

    return 1;
}

// Handles Htpp events and is in charge of buffering source_buf (jpg compressed image)
esp_err_t _http_event_handler(esp_http_client_event_t* evt) {
    static uint32_t data_recv = 0;
    static uint32_t data_len_total = 0;
    static uint32_t on_data_cnt = 0;
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
#if DEBUG_VERBOSE
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
#endif
            if (strncmp(evt->header_key, "Content-Length", 14) == 0) {
                // Should be big enough to hold the JPEG file size
                data_len_total = atol(evt->header_value);
                ESP_LOGI("epdiy", "Allocating content buffer of length %X (%dKiB)", data_len_total, data_len_total / 1024);

                source_buf = (uint8_t*)heap_caps_malloc(data_len_total, MALLOC_CAP_SPIRAM);
                // source_buf = (uint8_t*)heap_caps_malloc(data_len_total, MALLOC_CAP_INTERNAL);

                if (source_buf == NULL) {
                    ESP_LOGE("main", "Initial alloc source_buf failed!");
                }

                printf("Free heap after buffers allocation: %X\n", xPortGetFreeHeapSize());
            }

            break;
        case HTTP_EVENT_ON_DATA:
            ++on_data_cnt;
#if DEBUG_VERBOSE
            if (on_data_cnt % 10 == 0) {
                ESP_LOGI(TAG, "%d len:%d %d%%", on_data_cnt, evt->data_len, data_recv * 100 / data_len_total);
            }
#endif
            // should be allocated after the Content-Length header was received.
            assert(source_buf != NULL);

            if (on_data_cnt == 1)
                time_download_start = esp_timer_get_time();
            // Append received data into source_buf
            memcpy(&source_buf[data_recv], evt->data, evt->data_len);
            data_recv += evt->data_len;

            // Optional hexa dump
            // ESP_LOG_BUFFER_HEX(TAG, output_buffer, evt->data_len);
            break;

        case HTTP_EVENT_ON_FINISH:
            // Do not draw if it's a redirect (302)
            if (esp_http_client_get_status_code(evt->client) == 200) {
                printf("%" PRIu32 " bytes read from %s\n\n", data_recv, IMG_URL);
                drawBufJpeg(source_buf, 0, 0);
                // drawBufJpeg2(source_buf, 0, 0);
                time_download = (esp_timer_get_time() - time_download_start) / 1000;
                ESP_LOGI("www-dw", "%" PRIu32 " ms - download", time_download);
                // Refresh display
                epd_hl_update_screen(&hl, MODE_GC16, 25);

                ESP_LOGI(
                    "total", "%" PRIu32 " ms - total time spent\n",
                    time_download + time_decomp + time_render
                );
            }
            break;

        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED\n");
            break;

        default:
            ESP_LOGI(TAG, "HTTP_EVENT_ fallback caught event %d", evt->event_id);
    }
    return ESP_OK;
}

// Handles http request
static void http_post(void) {
    /**
     * NOTE: All the configuration parameters for http_client must be specified
     * either in URL or as host and path parameters.
     * FIX: Uncommenting cert_pem restarts even if providing the right certificate
     */
    esp_http_client_config_t config = {
        .url = IMG_URL,
        .event_handler = _http_event_handler,
        .buffer_size = HTTP_RECEIVE_BUFFER_SIZE,
        .disable_auto_redirect = false,
        .timeout_ms = HTTP_RECEIVE_TIMEOUT_MS,
#if VALIDATE_SSL_CERTIFICATE == true
        .cert_pem = (char*)server_cert_pem_start
#endif
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

#if DEBUG_VERBOSE
    printf("Free heap before HTTP download: %X\n", xPortGetFreeHeapSize());
    if (esp_http_client_get_transport_type(client) == HTTP_TRANSPORT_OVER_SSL && config.cert_pem) {
        printf("SSL CERT:\n%s\n\n", (char*)server_cert_pem_start);
    }
#endif

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        // esp_http_client_get_content_length returns a uint64_t in esp-idf v5, so it needs a %lld
        // format specifier
        ESP_LOGI(
            TAG, "\nIMAGE URL: %s\n\nHTTP GET Status = %d, content_length = %d\n", IMG_URL,
            esp_http_client_get_status_code(client), (int)esp_http_client_get_content_length(client)
        );
    } else {
        ESP_LOGE(TAG, "\nHTTP GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);

#if MILLIS_DELAY_BEFORE_SLEEP > 0
    vTaskDelay(MILLIS_DELAY_BEFORE_SLEEP / portTICK_PERIOD_MS);
#endif
    printf("Go to sleep %d minutes\n", DEEPSLEEP_MINUTES_AFTER_RENDER);
    epd_poweroff();
    deepsleep();
}

void app_main(void) {
  ESP_LOGI(TAG, "START!");
  enum EpdInitOptions init_options = EPD_LUT_64K;
    // For V6 and below, try to use less memory. V7 queue uses less anyway.
#ifdef CONFIG_IDF_TARGET_ESP32
    init_options |= EPD_FEED_QUEUE_8;
#endif
    epd_init(&DEMO_BOARD, &DISPLAY_SCREEN_TYPE, init_options);
    // epd_set_vcom(1560);
    hl = epd_hl_init(WAVEFORM);
    fb = epd_hl_get_framebuffer(&hl);
    epd_set_rotation(DISPLAY_ROTATION);

    generate_gamme(0.7);

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // WiFi log level set only to Error otherwise outputs too much
    esp_log_level_set("wifi", ESP_LOG_ERROR);

    // Initialization: WiFi + clean screen while downloading image
    wifi_init_sta();
#if VALIDATE_SSL_CERTIFICATE == true
    obtain_time();
#endif
    epd_poweron();
    epd_fullclear(&hl, TEMPERATURE);

    // handle http request
    http_post();
}
