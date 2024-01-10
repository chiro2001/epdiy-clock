#include "fb_save_load.h"
#include "common.h"
#include "compress.h"

const static char *TAG = "fb_save_load";
// static esp_err_t (*compress_mem_to_file)(const char *, const uint8_t *,
// size_t, int) = compress_mem_to_file_miniz; static esp_err_t
// (*decompress_file_to_mem)(const char *, uint8_t *, size_t) =
//     decompress_file_to_mem_miniz;
static esp_err_t (*compress_mem_to_file)(const char *, const uint8_t *, size_t,
                                         int) = compress_mem_to_file_zlib;
static esp_err_t (*decompress_file_to_mem)(const char *, uint8_t *, size_t) =
    decompress_file_to_mem_zlib;

esp_err_t fb_save_raw() {
  // save framebuffer to file
  FILE *fp = fopen(filename_fb, "wb");
  if (!fp) {
    ESP_LOGE(TAG, "Failed to open file for writing");
    return ESP_FAIL;
  }
  int fb_size = epd_width() / 2 * epd_height();
  ESP_LOGI(TAG, "Writing %d bytes (%d KiB) to %s", fb_size * 4,
           fb_size * 4 / 1024, filename_fb);
  unsigned int wr;
  wr = fwrite(hl.front_fb, 1, fb_size, fp);
  if (wr != fb_size) {
    ESP_LOGE(TAG, "fwrite front fb failed! expected %d, got %d", fb_size, wr);
  }
  wr = fwrite(hl.back_fb, 1, fb_size, fp);
  if (wr != fb_size) {
    ESP_LOGE(TAG, "fwrite back fb failed! expected %d, got %d", fb_size, wr);
  }
  wr = fwrite(hl.difference_fb, 1, fb_size * 2, fp);
  if (wr != fb_size * 2) {
    ESP_LOGE(TAG, "fwrite difference fb failed! expected %d, got %d",
             fb_size * 2, wr);
  }
  fclose(fp);
  return ESP_OK;
}

esp_err_t fb_save_compressed() {
  // save framebuffer to file
  int fb_size = epd_width() / 2 * epd_height();
  ESP_LOGI(TAG, "Writing %d bytes (%d KiB) from mem", fb_size * 4,
           fb_size * 4 / 1024);

  esp_err_t r;
  if (ESP_OK !=
      (r = compress_mem_to_file(filename_fb_compressed_front, hl.front_fb,
                                fb_size, FRAME_COMPRESS_LEVEL))) {
    ESP_LOGE(TAG, "compress_mem_to_file front failed!");
    return r;
  }
  if (ESP_OK !=
      (r = compress_mem_to_file(filename_fb_compressed_back, hl.back_fb,
                                fb_size, FRAME_COMPRESS_LEVEL))) {
    ESP_LOGE(TAG, "compress_mem_to_file back failed!");
    return r;
  }
  if (ESP_OK !=
      (r = compress_mem_to_file(filename_fb_compressed_diff, hl.difference_fb,
                                fb_size * 2, FRAME_COMPRESS_LEVEL))) {
    ESP_LOGE(TAG, "compress_mem_to_file difference failed!");
    return r;
  }
  return ESP_OK;
}

esp_err_t fb_load() {
  // load framebuffer from file
  FILE *fp = fopen(filename_fb, "rb");
  if (!fp) {
    ESP_LOGE(TAG, "Failed to open file for reading");
    return ESP_FAIL;
  }
  int fb_size = epd_width() / 2 * epd_height();
  ESP_LOGI(TAG, "Reading %d bytes (%d KiB) from %s", fb_size * 4,
           fb_size * 4 / 1024, filename_fb);
  unsigned int rd;
  rd = fread(hl.front_fb, 1, fb_size, fp);
  if (rd != fb_size) {
    ESP_LOGE(TAG, "fread front fb failed! expected %d, got %d", fb_size, rd);
  }
  rd = fread(hl.back_fb, 1, fb_size, fp);
  if (rd != fb_size) {
    ESP_LOGE(TAG, "fread back fb failed! expected %d, got %d", fb_size, rd);
  }
  rd = fread(hl.difference_fb, 1, fb_size * 2, fp);
  if (rd != fb_size * 2) {
    ESP_LOGE(TAG, "fread difference fb failed! expected %d, got %d",
             fb_size * 2, rd);
  }
  return ESP_OK;
}

esp_err_t fb_load_compressed() {
  // load framebuffer from file
  int fb_size = epd_width() / 2 * epd_height();
  esp_err_t r;
  if (ESP_OK != (r = decompress_file_to_mem(filename_fb_compressed_front,
                                            hl.front_fb, fb_size))) {
    ESP_LOGE(TAG, "decompress_file_to_mem front failed!");
    return r;
  }
  if (ESP_OK != (r = decompress_file_to_mem(filename_fb_compressed_back,
                                            hl.back_fb, fb_size))) {
    ESP_LOGE(TAG, "decompress_file_to_mem back failed!");
    return r;
  }
  if (ESP_OK != (r = decompress_file_to_mem(filename_fb_compressed_diff,
                                            hl.difference_fb, fb_size * 2))) {
    ESP_LOGE(TAG, "decompress_file_to_mem difference failed!");
    return r;
  }
  return ESP_OK;
}

esp_err_t fb_load_compressed_file(const char *filename, uint8_t *dest) {
  // load framebuffer from file
  int fb_size = epd_width() / 2 * epd_height();
  esp_err_t r;
  if (ESP_OK != (r = decompress_file_to_mem(filename, dest, fb_size))) {
    ESP_LOGE(TAG, "decompress_file_to_mem %s failed!", filename);
    return r;
  }
  return ESP_OK;
}
