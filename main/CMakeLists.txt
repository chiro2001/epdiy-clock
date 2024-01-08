file(GLOB SRCS *.c)

idf_component_register(
  SRCS ${SRCS}
  INCLUDE_DIRS "."
  REQUIRES 
    epdiy
    esp_jpeg
    esp_rom
    nvs_flash esp-tls esp_http_client
    esp_app_format
    esp_wifi
)

# make compliler happy
idf_build_set_property(COMPILE_OPTIONS "-Wformat=0" APPEND)