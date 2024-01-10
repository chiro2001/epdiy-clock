#ifndef __SETTINGS_H__
#define __SETTINGS_H__

/// hardware
#define DEMO_BOARD epd_board_v5
#define WAVEFORM EPD_BUILTIN_WAVEFORM
#define DISPLAY_ROTATION EPD_ROT_LANDSCAPE
#define DISPLAY_SCREEN_TYPE ED060KD1
// #define DISPLAY_SCREEN_TYPE ED060XC3
#define TEMPERATURE 25

#define EPDIY_USE_HIMEM 1

/// Deepsleep configuration
#define DEEPSLEEP_MINUTES_AFTER_RENDER 2

/// ssl
// #define VALIDATE_SSL_CERTIFICATE 1
#define VALIDATE_SSL_CERTIFICATE 0

/// source
// #define IMG_URL ("http://192.168.31.141:8080/70e412fc8067fa65798ec2f763c62373.jpg")
// #define IMG_URL ("https://loremflickr.com/1448/1072")

#define IMG_URL "https://img.moehu.org/pic.php?id=img1"
#define IMG_HOST "img.moehu.org"

// #define IMG_URL "https://loremflickr.com/1448/1072"
// #define IMG_HOST "loremflickr.com"

// #define IMG_URL "http://192.168.31.141:8080/70e412fc8067fa65798ec2f763c62373.jpg"
// #define IMG_HOST "192.168.31.141"

// #define IMG_URL "http://192.168.31.141:8080/diannnnnna.png"
// #define IMG_HOST "192.168.31.141"

// #define IMG_URL "https://www.howsmyssl.com/a/check"
// #define IMG_HOST "www.howsmyssl.com"

/// image decode
#define JPG_DITHERING 0

/// wifi
#define ESP_WIFI_SSID "504B"
#define ESP_WIFI_PASSWORD "2001106504B"

/// network
// #define HTTP_RECEIVE_BUFFER_SIZE 1986
#define HTTP_RECEIVE_BUFFER_SIZE (1024 * 8)
// plz set CONFIG_LWIP_SNTP_MAX_SERVERS=3
// #define NTP_SERVER_CONFIG ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(3,
//                           ESP_SNTP_SERVER_LIST("ntp.chiro.work", "cn.pool.ntp.org", "time.windows.com") )
#define NTP_SERVER_CONFIG ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(1, \
                          ESP_SNTP_SERVER_LIST("ntp.chiro.work") )
#define HTTP_RECEIVE_TIMEOUT_MS (30 * 1000)
// #define TIME_SERVER_URL "https://currentmillis.com/time/minutes-since-unix-epoch.php"
// #define TIME_SERVER_HOST "currentmillis.com"
#define TIME_SERVER_URL "http://worldtimeapi.org/api/timezone/Asia/Shanghai"
#define TIME_SERVER_HOST "worldtimeapi.org"

/// time
#define TIME_ZONE "CST-8"
#define TIME_FMT "%H:%M:%S"
// #define TIME_FMT "%H:%M"

/// storage
static const char *storage_base_path = "/spiflash";
static const char *storage_partition_label = "storage";
static const char *filename_current_image = "/spiflash/current";
static const char *filename_fallback_image = "/spiflash/fallback";
static const char *filename_temp_image = "/spiflash/temp";
static const char *filename_fb = "/spiflash/fb.raw";
static const char *filename_fb_compressed_front = "/spiflash/fb_front.miniz";
static const char *filename_fb_compressed_back = "/spiflash/fb_back.miniz";
static const char *filename_fb_compressed_diff = "/spiflash/fb_diff.miniz";
static const char *filename_last_time = "/spiflash/last_time.dat";

#define FRAME_COMPRESS_LEVEL Z_BEST_SPEED
// #define FRAME_COMPRESS_LEVEL Z_NO_COMPRESSION

#endif