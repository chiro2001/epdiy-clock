#ifndef __SETTINGS_H__
#define __SETTINGS_H__

/// hardware
#define DEMO_BOARD epd_board_v5
#define WAVEFORM EPD_BUILTIN_WAVEFORM
#define DISPLAY_ROTATION EPD_ROT_LANDSCAPE
#define DISPLAY_SCREEN_TYPE ED060KD1
#define TEMPERATURE 25

/// Deepsleep configuration
#define MILLIS_DELAY_BEFORE_SLEEP 2000
#define DEEPSLEEP_MINUTES_AFTER_RENDER 6

/// ssl
#define VALIDATE_SSL_CERTIFICATE 1

/// source
// #define IMG_URL ("http://192.168.31.141:8080/70e412fc8067fa65798ec2f763c62373.jpg")
// #define IMG_URL ("https://loremflickr.com/1448/1072")

// #define IMG_URL "https://img.moehu.org/pic.php?id=img1"
// #define IMG_HOST "img.moehu.org"
#define IMG_URL "https://www.howsmyssl.com/a/check"
#define IMG_HOST "www.howsmyssl.com"


/// image decode
#define JPG_DITHERING 0

/// wifi
#define ESP_WIFI_SSID "504B"
#define ESP_WIFI_PASSWORD "2001106504B"

/// network
#define HTTP_RECEIVE_BUFFER_SIZE 1986
// plz set CONFIG_LWIP_SNTP_MAX_SERVERS=3
#define NTP_SERVER_CONFIG ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(3, \
                          ESP_SNTP_SERVER_LIST("cn.pool.ntp.org", "time.windows.com", "ntp.chiro.work") )
#define HTTP_RECEIVE_TIMEOUT_MS (30 * 1000)

#endif