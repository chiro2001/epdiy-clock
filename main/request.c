#include "common.h"
#include "request.h"

const char *TAG = "request";

esp_err_t https_get_request(esp_tls_cfg_t cfg, const char *url,
                                   const char *REQUEST, char **res_buf,
                                   uint32_t *ret_len) {
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
  /* The TLS session is successfully established, now saving the session ctx for
   * reuse */
  if (save_client_session) {
    esp_tls_free_client_session(tls_client_session);
    tls_client_session = esp_tls_get_client_session(tls);
  }
#endif

  size_t written_bytes = 0;
  do {
    ret = esp_tls_conn_write(tls, REQUEST + written_bytes,
                             strlen(REQUEST) - written_bytes);
    if (ret >= 0) {
      ESP_LOGI(TAG, "%d bytes written", ret);
      written_bytes += ret;
    } else if (ret != ESP_TLS_ERR_SSL_WANT_READ &&
               ret != ESP_TLS_ERR_SSL_WANT_WRITE) {
      ESP_LOGE(TAG, "esp_tls_conn_write  returned: [0x%02X](%s)", ret,
               esp_err_to_name(ret));
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
    *res_buf = (char *)heap_caps_malloc(bytes_to_read, MALLOC_CAP_SPIRAM);
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

    if (ret == ESP_TLS_ERR_SSL_WANT_WRITE || ret == ESP_TLS_ERR_SSL_WANT_READ) {
      continue;
    } else if (ret < 0) {
      ESP_LOGE(TAG, "esp_tls_conn_read  returned [-0x%02X](%s)", -ret,
               esp_err_to_name(ret));
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