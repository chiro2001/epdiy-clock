/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "lwip/dns.h"
#include "lwip/err.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "time_sync.h"

#include "request.h"
#include "settings.h"

static const char *TAG = "time_sync";

extern const uint8_t
    time_server_cert_pem_start[] asm("_binary_time_server_cert_pem_start");
extern const uint8_t
    time_server_cert_pem_end[] asm("_binary_time_server_cert_pem_end");

#define STORAGE_NAMESPACE "storage"

void print_time() {
  // get from time()
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  char datetime_buf[48];
  strftime(datetime_buf, sizeof(datetime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
  ESP_LOGI(TAG, "The current date/time is: %s", datetime_buf);
}

void initialize_sntp(void) {
  static bool initialized = false;
  if (initialized) {
    return;
  }
  ESP_LOGI(TAG, "Initializing SNTP");
  esp_sntp_config_t config = NTP_SERVER_CONFIG;
  esp_netif_sntp_init(&config);
  initialized = true;
}

static esp_err_t obtain_time(void) {
  // wait for time to be set
  int retry = 0;
  const int retry_count = 10;
  while (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(2000)) != ESP_OK &&
         ++retry < retry_count) {
    ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry,
             retry_count);
  }
  if (retry == retry_count) {
    return ESP_FAIL;
  }
  return ESP_OK;
}

static esp_err_t obtain_time_https(void) {
  esp_tls_cfg_t cfg = {
      .cacert_buf = (const unsigned char *)time_server_cert_pem_start,
      .cacert_bytes = time_server_cert_pem_end - time_server_cert_pem_start,
  };
  const char *request = "GET / HTTP/1.1\r\n"
                        "Host: " TIME_SERVER_HOST "\r\n"
                        "User-Agent: esp-idf/1.0 esp32\r\n"
                        "\r\n";
  char *request_buf = NULL;
  request_buf = malloc(1024);
  if (request_buf == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory for request buffer");
    return ESP_FAIL;
  }
  uint32_t request_len = 0;
  esp_err_t r = https_get_request(cfg, TIME_SERVER_URL, request,
                                  (char **)&request_buf, &request_len);
  if (r != ESP_OK) {
    ESP_LOGE(TAG, "Error fetching time from server");
    free(request_buf);
    return r;
  }
  ESP_LOGI(TAG, "r=%d, request_len=%d, message: %s", r, request_len,
           request_buf);
  uint64_t unix_time = 0;
  sscanf(request_buf, "%lld", &unix_time);
  ESP_LOGI(TAG, "unix_time=%lld", unix_time);
  if (unix_time == 0) {
    ESP_LOGE(TAG, "Error parsing time from server");
  } else {
    struct timeval set_time;
    set_time.tv_sec = unix_time;
    settimeofday(&set_time, NULL);
    print_time();
  }
  free(request_buf);
  return r;
}

esp_err_t _time_http_event_handler(esp_http_client_event_t *evt) {
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
    ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key,
             evt->header_value);
    break;
  case HTTP_EVENT_ON_DATA:
    ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
    // write to user_data, simply short string
    memcpy(evt->user_data, evt->data, evt->data_len);
    break;
  case HTTP_EVENT_ON_FINISH:
    ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
    break;
  case HTTP_EVENT_DISCONNECTED:
  case HTTP_EVENT_REDIRECT:
    break;
  }
  return ESP_OK;
}

static esp_err_t obtain_time_http(void) {
  esp_http_client_config_t config = {
    .url = TIME_SERVER_URL,
    .event_handler = _time_http_event_handler,
    .buffer_size = HTTP_RECEIVE_BUFFER_SIZE,
    .disable_auto_redirect = false,
    .timeout_ms = HTTP_RECEIVE_TIMEOUT_MS,
#if VALIDATE_SSL_CERTIFICATE
    .cert_pem = (char *)time_server_cert_pem_start
#endif
  };
  uint8_t *buf = malloc(HTTP_RECEIVE_BUFFER_SIZE);
  if (buf == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory for request buffer");
    return ESP_FAIL;
  }
  memset(buf, 0x00, HTTP_RECEIVE_BUFFER_SIZE);
  esp_http_client_handle_t client = esp_http_client_init(&config);
  esp_http_client_set_user_data(client, buf);
  esp_err_t err = esp_http_client_perform(client);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error fetching time from server");
    free(buf);
    return err;
  }
  ESP_LOGI(TAG, "message: %s", buf);
  uint64_t unix_minute_time = 0;
  sscanf((char *)buf, "%lld", &unix_minute_time);
  ESP_LOGI(TAG, "unix_minute_time=%lld", unix_minute_time);
  if (unix_minute_time == 0) {
    ESP_LOGE(TAG, "Error parsing time from server");
    err = ESP_FAIL;
  } else {
    struct timeval set_time;
    set_time.tv_sec = unix_minute_time * 60;
    settimeofday(&set_time, NULL);
    print_time();
  }
  free(buf);
  return err;
}

void set_time_zone(void) {
  setenv("TZ", TIME_ZONE, 1);
  tzset();
}

esp_err_t fetch_and_store_time_in_nvs(void *args) {
  // initialize_sntp();
  // if (obtain_time() != ESP_OK) {
  //     return ESP_FAIL;
  // }

  if (obtain_time_http() != ESP_OK) {
    return ESP_FAIL;
  }

  nvs_handle_t my_handle;
  esp_err_t err;

  time_t now;
  time(&now);

  // Open
  err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
  if (err != ESP_OK) {
    goto exit;
  }

  // Write
  err = nvs_set_i64(my_handle, "timestamp", now);
  if (err != ESP_OK) {
    goto exit;
  }

  err = nvs_commit(my_handle);
  if (err != ESP_OK) {
    goto exit;
  }

  nvs_close(my_handle);
  esp_netif_deinit();

exit:
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error updating time in nvs");
  } else {
    ESP_LOGI(TAG, "Updated time in NVS");
  }
  return err;
}

esp_err_t update_time_from_nvs(void) {
  nvs_handle_t my_handle;
  esp_err_t err;

  err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error opening NVS");
    goto exit;
  }

  int64_t timestamp = 0;

  err = nvs_get_i64(my_handle, "timestamp", &timestamp);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGI(TAG, "Time not found in NVS. Syncing time from SNTP server.");
    if (fetch_and_store_time_in_nvs(NULL) != ESP_OK) {
      err = ESP_FAIL;
    } else {
      err = ESP_OK;
    }
  } else if (err == ESP_OK) {
    struct timeval get_nvs_time;
    get_nvs_time.tv_sec = timestamp;
    settimeofday(&get_nvs_time, NULL);
  }

exit:
  nvs_close(my_handle);
  return err;
}
