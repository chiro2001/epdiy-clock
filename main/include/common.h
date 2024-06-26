#ifndef __COMMON_H__
#define __COMMON_H__

#include "settings.h"

#include <stdlib.h>
#include <sys/types.h>
#include <math.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "settings.h"
// WiFi related
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "nvs_flash.h"
// HTTP Client + time
#include "esp_http_client.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_tls.h"
#include "esp_err.h"
// epdiy
#include "epdiy.h"
#include "epd_highlevel.h"
#include "epd_display.h"
// storage
#include "esp_vfs.h"
// #include "esp_vfs_fat.h"
#include "esp_spiffs.h"
#include "zlib.h"
// font
#include "font/time_traveler.h"

// image decoders
#include "jpeg_decoder.h"
// JPG decoder is on ESP32 rom for this version
#if ESP_IDF_VERSION_MAJOR >= 4  // IDF 4+
#include "esp32/rom/tjpgd.h"
#else  // ESP32 Before IDF 4.0
#include "rom/tjpgd.h"
#endif
#include "pngle.h"

void deepsleep();

extern EpdiyHighlevelState hl;

#endif