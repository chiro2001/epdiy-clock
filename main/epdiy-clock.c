#include "common.h"
#include "esp_err.h"
#include "settings.h"
#include <assert.h>
#include <unistd.h>

/// global variables

EpdiyHighlevelState hl;
uint32_t data_len_total = 0;
// #define TAG "eclock"
const static char *TAG = "eclock";

// buffers
uint8_t* source_buf = NULL;       // downloaded image
static uint8_t tjpgd_work[3096];  // tjpgd 3096 is the minimum size
uint8_t* fb;                      // EPD 2bpp buffer
static uint32_t feed_buffer_pos = 0;

// opened files
FILE *fp_downloading = NULL;
FILE *fp_reading = NULL;

// Load the EMBED_TXTFILES. Then doing (char*) server_cert_pem_start you get the SSL certificate
// Reference:
// https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html#embedding-binary-data
extern const uint8_t server_cert_pem_start[] asm("_binary_server_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_server_cert_pem_end");

// JPEG decoder
JDEC jd;
JRESULT rc;
// PNG decoder
pngle_t *pngle = NULL;
uint8_t render_pixel_skip = 0xff;

// Handle of the wear levelling library instance
// static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

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
    uint32_t image_height = jd->height;
    uint8_t* bitmap_ptr = (uint8_t*)bitmap;

    // Write to display
    int padding_x = (epd_rotated_display_width() - image_width) / 2;
    int padding_y = (epd_rotated_display_height() - image_height) / 2;
    
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
        if (yy < 0 || yy >= image_height) {
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

    while (count < nd) {
        if (buff != NULL) {
            *buff++ = source_buf[feed_buffer_pos];
        }
        count++;
        feed_buffer_pos++;
    }

    return count;
}

int draw_jpeg(uint8_t* source_buf) {
    feed_buffer_pos = 0;
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

    ESP_LOGI("JPG", "width: %d height: %d", jd.width, jd.height);
    ESP_LOGI("decode", "%" PRIu32 " ms . image decompression", time_decomp);

    return 0;
}

static uint32_t feed_buffer_file(
    JDEC* jd,
    uint8_t* buff,  // Pointer to the read buffer (NULL:skip)
    uint32_t nd
) {
    assert(fp_reading != NULL);
    uint32_t count = 0;
    if (feof(fp_reading)) {
        // printf("EOF\n");
        return count;
    } else if (!buff) {
        // just move the file pointer
        // printf("skip %d bytes\n", nd);
        fseek(fp_reading, nd, SEEK_CUR);
        count = nd;
    } else {
        // normal read
        count = fread(buff, 1, nd, fp_reading);
        // printf("read %d bytes, got %d\n", nd, count);
    }
    return count;
}

esp_err_t draw_jpeg_file(const char *filename) {
    fp_reading = fopen(filename, "rb");
    if (!fp_reading) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return ESP_FAIL;
    }
    rc = jd_prepare(&jd, feed_buffer_file, tjpgd_work, sizeof(tjpgd_work), NULL);
    if (rc != JDR_OK) {
        ESP_LOGE(TAG, "JPG jd_prepare error: %s", jd_errors[rc]);
        return ESP_FAIL;
    }

    uint32_t decode_start = esp_timer_get_time();
    vTaskDelay(0);
    // Last parameter scales        v 1 will reduce the image
    rc = jd_decomp(&jd, tjd_output, 0);
    if (rc != JDR_OK) {
        ESP_LOGE(TAG, "JPG jd_decomp error: %s", jd_errors[rc]);
        return ESP_FAIL;
    }
    vTaskDelay(0);
    time_decomp = (esp_timer_get_time() - decode_start) / 1000;

    ESP_LOGI("JPG", "width: %d height: %d", jd.width, jd.height);
    ESP_LOGI("decode", "%" PRIu32 " ms . image decompression", time_decomp);

    return ESP_OK;
}

void on_draw_png(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t rgba[4]) {
    uint32_t r = rgba[0]; // 0 - 255
    uint32_t g = rgba[1]; // 0 - 255
    uint32_t b = rgba[2]; // 0 - 255
    uint32_t a = rgba[3]; // 0: fully transparent, 255: fully opaque

    uint32_t image_width = pngle_get_width(pngle);
    uint32_t image_height = pngle_get_height(pngle);
    int epd_width = epd_rotated_display_width();
    int epd_height = epd_rotated_display_height();

    if (render_pixel_skip == 0xff) {
        render_pixel_skip = 0;
        if (image_width > epd_width * 2 || image_height > epd_height * 2) {
            render_pixel_skip = 2;
        }
        return;
    }
    if (render_pixel_skip) {
        image_width = image_width / render_pixel_skip;
        image_height = image_height / render_pixel_skip;
    }

    int padding_x = (epd_width - image_width) / 2;
    int padding_y = (epd_height - image_height) / 2;

    // if (a == 0) {
    //     // skip transparent pixels
    //     return;
    // }

    uint32_t val = (r * 38 + g * 75 + b * 15) >> 7;  // @vroland recommended formula
    // use alpha in white background
    val = (a == 0) ? 255 : val;
    // val = (val * 256 / (256 - a)) & 0xff;
    uint8_t color = gamme_curve[val];

    // print info
    // static int cnt = 0;
    // if (cnt % 100 == 0)
    //     ESP_LOGI("PNG", "x: %d y: %d w: %d h: %d r: %d g: %d b: %d a: %d val: %d px: %d py: %d color: %x", 
    //     x, y, w, h, r, g, b, a, val, padding_x, padding_y, color);
    // cnt++;

    if (render_pixel_skip == 0) {
        for (uint32_t yy = 0; yy < h; yy++) {
            for (uint32_t xx = 0; xx < w; xx++) {
                epd_draw_pixel(xx + x + padding_x, yy + y + padding_y, color, fb);
            }
        }
    } else {
        for (uint32_t yy = 0; yy < h; yy++) {
            for (uint32_t xx = 0; xx < w; xx++) {
                int xxx = xx + x;
                int yyy = yy + y;
                if (xxx % render_pixel_skip != 0 || yyy % render_pixel_skip != 0) {
                    continue;
                }
                epd_draw_pixel(xxx / render_pixel_skip + padding_x, 
                yyy / render_pixel_skip + padding_y, color, fb);
            }
        }
    }
}

int draw_png(uint8_t* source_buf, size_t size) {
    int r = 0;
    uint32_t decode_start = esp_timer_get_time();
    if (pngle != NULL) {
        pngle_destroy(pngle);
        pngle = NULL;
    }
    pngle = pngle_new();
    pngle_set_draw_callback(pngle, on_draw_png);

    r = pngle_feed(pngle, source_buf, size);
    if (r < 0) {
        ESP_LOGE(TAG, "PNG pngle_feed error: %d %s", r, pngle_error(pngle));
        return ESP_FAIL;
    }
    time_decomp = (esp_timer_get_time() - decode_start) / 1000;
    ESP_LOGI("PNG", "width: %d height: %d", pngle_get_width(pngle), pngle_get_height(pngle));
    ESP_LOGI("decode", "%" PRIu32 " ms . image decompression", time_decomp);
    return r;
}

esp_err_t display_source_buf() {
    if (!source_buf) {
        ESP_LOGW(TAG, "source_buf is NULL");
        return ESP_FAIL;
    }
    epd_fullclear(&hl, TEMPERATURE);
    ESP_LOGI(TAG, "%" PRIu32 " bytes read from %s", data_len_total, IMG_URL);
    int r = draw_jpeg(source_buf);
    if (r == ESP_FAIL) {
        ESP_LOGE(TAG, "draw as jpg failed, try to draw as png");
        r = draw_png(source_buf, data_len_total);
    }
    if (r == ESP_FAIL) {
        ESP_LOGE(TAG, "draw as png failed");
        return ESP_FAIL;
    }
    time_download = (esp_timer_get_time() - time_download_start) / 1000;
    ESP_LOGI(TAG, "%" PRIu32 " ms - download", time_download);
    // Refresh display
    epd_hl_update_screen(&hl, MODE_GC16, 25);

    ESP_LOGI(
        "total", "%" PRIu32 " ms - total time spent\n",
        time_download + time_decomp + time_render
    );
    return ESP_OK;
}

// Download and write data to `filename_temp_image'
esp_err_t _http_event_handler(esp_http_client_event_t* evt) {
    static uint32_t data_recv = 0;
    static uint32_t on_data_cnt = 0;
    static bool download_err = false;
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
// #if DEBUG_VERBOSE
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
// #endif
            if (strncmp(evt->header_key, "Content-Length", 14) == 0) {
                // Should be big enough to hold the JPEG file size
                data_len_total = atol(evt->header_value);
                if (data_len_total) {
                    // ESP_LOGI("epdiy", "Allocating content buffer of length %X (%dKiB)", data_len_total, data_len_total / 1024);
                    // source_buf = (uint8_t*)heap_caps_malloc(data_len_total, MALLOC_CAP_SPIRAM);
                    // // source_buf = (uint8_t*)heap_caps_malloc(data_len_total, MALLOC_CAP_INTERNAL);
                    // if (source_buf == NULL) {
                    //     ESP_LOGE("main", "Initial alloc source_buf failed!");
                    // }
                    // printf("Free heap after buffers allocation: %X\n", xPortGetFreeHeapSize());
                } else {
                    ESP_LOGW("main", "Content-Length header is empty!");
                }
            }
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
            break;
        case HTTP_EVENT_ON_DATA:
            ++on_data_cnt;
// #if DEBUG_VERBOSE
            if (on_data_cnt % 10 == 0) {
                ESP_LOGI(TAG, "%d len:%d %d%%", on_data_cnt, evt->data_len, data_recv * 100 / data_len_total);
            }
// #endif
            // should be allocated after the Content-Length header was received.
            // assert(source_buf != NULL);

            if (on_data_cnt == 1) {
                time_download_start = esp_timer_get_time();
                if (!fp_downloading) {
                    fp_downloading = fopen(filename_temp_image, "wb");
                    if (!fp_downloading) {
                        ESP_LOGE(TAG, "Failed to open file for writing");
                        download_err = true;
                    }
                }    
            }
            // Append received data into source_buf
            // memcpy(&source_buf[data_recv], evt->data, evt->data_len);
            // Write received data into file
            if (!fp_downloading) {
                ESP_LOGE(TAG, "fp_downloading is NULL");
            } else {
                unsigned long written_bytes = fwrite(evt->data, 1, evt->data_len, fp_downloading);
                if (written_bytes != evt->data_len) {
                    ESP_LOGE(TAG, "fwrite failed! expected %d, got %d", evt->data_len, written_bytes);
                    download_err = true;
                }
            }
            data_recv += evt->data_len;

            // Optional hexa dump
            // ESP_LOG_BUFFER_HEX(TAG, output_buffer, evt->data_len);
            break;

        case HTTP_EVENT_ON_FINISH:
            // Do not draw if it's a redirect (302)
            if (esp_http_client_get_status_code(evt->client) == 200) {
                data_len_total = data_recv;
                esp_err_t r = display_source_buf();
                if (r != ESP_OK) {
                    ESP_LOGE(TAG, "display_source_buf failed");
                    // return r;
                }
            }
            break;

        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED\n");
            // close file
            if (fp_downloading) {
                fclose(fp_downloading);
                fp_downloading = NULL;
                if (download_err) {
                    ESP_LOGE(TAG, "Download failed, deleting file");
                    unlink(filename_temp_image);
                }
            }
            break;

        default:
            ESP_LOGI(TAG, "HTTP_EVENT_ fallback caught event %d", evt->event_id);
    }
    return ESP_OK;
}

static const char HOWSMYSSL_REQUEST[] = "GET " IMG_URL " HTTP/1.1\r\n"
                             "Host: " IMG_HOST "\r\n"
                             "User-Agent: esp-idf/1.0 esp32\r\n"
                             "\r\n";

static esp_err_t https_get_request(esp_tls_cfg_t cfg, const char *url, const char *REQUEST, char **res_buf, uint32_t *ret_len) {
    char buf[512];
    int ret, len;
    size_t recv_len = 0;
    esp_err_t err = ESP_OK;

    assert(res_buf);

    esp_tls_t *tls = esp_tls_init();
    if (!tls) {
        ESP_LOGE(TAG, "Failed to allocate esp_tls handle!");
        err = ESP_FAIL;
        goto exit;
    }

    if (esp_tls_conn_http_new_sync(url, &cfg, tls) == 1) {
        ESP_LOGI(TAG, "Connection established...");
    } else {
        ESP_LOGE(TAG, "Connection failed...");
        err = ESP_FAIL;
        goto cleanup;
    }

#ifdef CONFIG_EXAMPLE_CLIENT_SESSION_TICKETS
    /* The TLS session is successfully established, now saving the session ctx for reuse */
    if (save_client_session) {
        esp_tls_free_client_session(tls_client_session);
        tls_client_session = esp_tls_get_client_session(tls);
    }
#endif

    size_t written_bytes = 0;
    do {
        ret = esp_tls_conn_write(tls,
                                 REQUEST + written_bytes,
                                 strlen(REQUEST) - written_bytes);
        if (ret >= 0) {
            ESP_LOGI(TAG, "%d bytes written", ret);
            written_bytes += ret;
        } else if (ret != ESP_TLS_ERR_SSL_WANT_READ  && ret != ESP_TLS_ERR_SSL_WANT_WRITE) {
            ESP_LOGE(TAG, "esp_tls_conn_write  returned: [0x%02X](%s)", ret, esp_err_to_name(ret));
            err = ESP_FAIL;
            goto cleanup;
        }
    } while (written_bytes < strlen(REQUEST));

    ESP_LOGI(TAG, "Reading HTTP response...");
    ssize_t bytes_to_read;
    if ((bytes_to_read = esp_tls_get_bytes_avail(tls)) <= 0) {
        ESP_LOGE(TAG, "esp_tls_get_bytes_avail  returned: [0x%02X]", bytes_to_read);
        // err = ESP_FAIL;
        // goto cleanup;
    }
    if (!*res_buf && bytes_to_read) {
        ESP_LOGI(TAG, "Allocating %d bytes of memory...", bytes_to_read);
        *res_buf = (char*)heap_caps_malloc(bytes_to_read, MALLOC_CAP_SPIRAM);
        if (!*res_buf) {
            ESP_LOGE(TAG, "Failed to allocate memory for res_buf");
            err = ESP_FAIL;
            goto cleanup;
        }
    }
    do {
        len = sizeof(buf) - 1;
        memset(buf, 0x00, len);
        ret = esp_tls_conn_read(tls, (char *)buf, len);

        if (ret == ESP_TLS_ERR_SSL_WANT_WRITE  || ret == ESP_TLS_ERR_SSL_WANT_READ) {
            continue;
        } else if (ret < 0) {
            ESP_LOGE(TAG, "esp_tls_conn_read  returned [-0x%02X](%s)", -ret, esp_err_to_name(ret));
            break;
        } else if (ret == 0) {
            ESP_LOGI(TAG, "connection closed");
            break;
        }

        len = ret;
        ESP_LOGD(TAG, "%d bytes read", len);
        if (*res_buf) {
            memcpy((*res_buf) + recv_len, buf, len);
        } else {
            for (int i = 0; i < len; i++) {
                putchar(buf[i]);
            }
            putchar('\n'); // JSON output doesn't have a newline at end
        }
        recv_len += len;
    } while (1);

cleanup:
    esp_tls_conn_destroy(tls);
    *ret_len = recv_len;
exit:
    return err;
}

static esp_err_t https_get_request_using_cacert_buf(void)
{
    ESP_LOGI(TAG, "https_request using cacert_buf");
    esp_tls_cfg_t cfg = {
        .cacert_buf = (const unsigned char *) server_cert_pem_start,
        .cacert_bytes = server_cert_pem_end - server_cert_pem_start,
    };
    return https_get_request(cfg, IMG_URL, HOWSMYSSL_REQUEST, (char**)&source_buf, &data_len_total);
}

static esp_err_t https_get_request_using_global_ca_store(void)
{
    esp_err_t esp_ret = ESP_FAIL;
    ESP_LOGI(TAG, "https_request using global ca_store");
    esp_ret = esp_tls_set_global_ca_store(server_cert_pem_start, server_cert_pem_end - server_cert_pem_start);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Error in setting the global ca store: [%02X] (%s),could not complete the https_request using global_ca_store", esp_ret, esp_err_to_name(esp_ret));
        return esp_ret;
    }
    esp_tls_cfg_t cfg = {
        .use_global_ca_store = true,
    };
    esp_ret = https_get_request(cfg, IMG_URL, HOWSMYSSL_REQUEST, (char**)&source_buf, &data_len_total);
    esp_tls_free_global_ca_store();
    return esp_ret;
}

static esp_err_t https_request(void) {
    esp_err_t r = ESP_FAIL;
    printf("SSL CERT:\n%s\n\n", (char*)server_cert_pem_start);
    ESP_LOGI(TAG, "Minimum free heap size: %" PRIu32 " bytes", esp_get_minimum_free_heap_size());
    // r = https_get_request_using_cacert_buf();
    r = https_get_request_using_global_ca_store();
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "https_get_request failed");
        return r;
    }
    if (!source_buf) {
        ESP_LOGE(TAG, "source_buf is NULL");
        return ESP_FAIL;
    }
    r = display_source_buf();
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "display_source_buf failed");
        return r;
    }
    return ESP_OK;
}

// Handles http request
static esp_err_t http_request(void) {
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
#if VALIDATE_SSL_CERTIFICATE
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
    return err;
}

void print_time() {
    // get from time()
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    ESP_LOGI(TAG, "The current date/time is: %s", asctime(&timeinfo));
}

void finish_system(void) {
    epd_poweroff();
    epd_deinit();
    esp_vfs_spiffs_unregister(storage_partition_label);
    deepsleep();
}

esp_err_t download_image() {
    // handle http request
    // esp_err_t r = http_request();
    // esp_err_t r = https_request();
    esp_err_t r;
    int retry = 3;
    do {
        retry--;
        r = http_request();
        if (r != ESP_OK) {
            ESP_LOGE(TAG, "http_post failed, retrying, retry: %d", retry);
            if (retry == 0) {
                break;
            }
            fetch_and_store_time_in_nvs(NULL);
            print_time();
        } else {
            break;
        }
    } while (r != ESP_OK && retry > 0);
    return r;
}

// esp_err_t init_flash_storage_fatfs(const char *partition_label) {
//     ESP_LOGI(TAG, "Mounting FAT filesystem");
//     const esp_vfs_fat_mount_config_t mount_config = {
//             .max_files = 4,
//             .format_if_mount_failed = true,
//             .allocation_unit_size = CONFIG_WL_SECTOR_SIZE
//     };
//     esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(storage_base_path, partition_label, &mount_config, &s_wl_handle);
//     if (err != ESP_OK) {
//         ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
//         return err;
//     }
//     return ESP_OK;
// }

esp_err_t init_flash_storage() {
    ESP_LOGI(TAG, "Initializing SPIFFS");
    esp_vfs_spiffs_conf_t conf = {
        .base_path = storage_base_path,
        .partition_label = storage_partition_label,
        .max_files = 12,
        .format_if_mount_failed = true
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

esp_err_t do_display(const char *filename) {
    epd_fullclear(&hl, TEMPERATURE);
    ESP_LOGI(TAG, "%" PRIu32 " bytes read from %s", data_len_total, IMG_URL);
    esp_err_t r = draw_jpeg_file(filename);
    if (r == ESP_FAIL) {
        // ESP_LOGE(TAG, "draw as jpg failed, try to draw as png");
        // r = draw_png(source_buf, data_len_total);
    }
    if (r == ESP_FAIL) {
        ESP_LOGE(TAG, "draw as png failed");
        return ESP_FAIL;
    }
    time_download = (esp_timer_get_time() - time_download_start) / 1000;
    ESP_LOGI(TAG, "%" PRIu32 " ms - download", time_download);
    // Refresh display
    epd_hl_update_screen(&hl, MODE_GC16, 25);

    ESP_LOGI(
        "total", "%" PRIu32 " ms - total time spent\n",
        time_download + time_decomp + time_render
    );
    return ESP_OK;
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

    // Initializaze Flash Storage
    ESP_ERROR_CHECK(init_flash_storage());

    // WiFi log level set only to Error otherwise outputs too much
    esp_log_level_set("wifi", ESP_LOG_ERROR);

    // Initialization: WiFi + clean screen while downloading image
    wifi_init_sta();

    print_time();
    // auto request and save time in NVS
    if (esp_reset_reason() == ESP_RST_POWERON) {
        fetch_and_store_time_in_nvs(NULL);
    }
    update_time_from_nvs();
    // print time now
    print_time();

    epd_poweron();

    // list files
    DIR *d;
    struct dirent *dir;
    d = opendir(storage_base_path);
    if (d) {
        ESP_LOGI(TAG, "Files in %s:", storage_base_path);
        while ((dir = readdir(d)) != NULL) {
            ESP_LOGI(TAG, "%s", dir->d_name);
        }
        closedir(d);
    }

    // struct stat st;
    // // display current image, or fallback
    // if (stat(filename_current_image, &st) == 0) {
    //     // file exists
    //     do_display(filename_current_image);
    // } else {
    //     // file doesn't exist
    //     if (stat(filename_fallback_image, &st) == 0) {
    //         // file exists
    //         do_display(filename_fallback_image);
    //     } else {
    //         // file doesn't exist
    //         ESP_LOGE(TAG, "No image to display");
    //         // finish_system();
    //         // return;
    //     }
    // }

    ret = download_image();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "download_image failed");
    } else {
        ESP_LOGI(TAG, "download_image success");
        // delete fallback image
        unlink(filename_fallback_image);
        // move current image to fallback image
        rename(filename_current_image, filename_fallback_image);
        // move temp file to final file
        rename(filename_temp_image, filename_current_image);
    }
    // then display current image, or fallback
    // xTaskCreate(do_display, "do_display", 8192, NULL, 5, NULL);
    do_display(filename_current_image);
    finish_system();
}
