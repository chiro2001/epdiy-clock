# file(GLOB app_sources ${project_dir}/*.c)
# message(STATUS "app_sources: ${app_sources}")
set(app_sources
  "epdiy-clock.c"
  "sleep.c"
  "time_sync.c"
  "wifi.c"
  "font/time_traveler.c"
  "compress.c"
  "fb_save_load.c"
)
# file(GLOB_RECURSE app_resources res/*)

idf_component_register(
  SRCS ${app_sources}
  INCLUDE_DIRS "include" "."
  REQUIRES 
    epdiy
    esp_jpeg
    esp_rom
    nvs_flash 
    esp-tls 
    esp_http_client
    esp_app_format
    esp_wifi
    # fatfs
    spiffs
    vfs
    pngle
    zlib
  EMBED_TXTFILES ${project_dir}/res/ssl_cert/server_cert.pem
)

# make compliler happy
# idf_build_set_property(COMPILE_OPTIONS "-Wformat=0" APPEND)
component_compile_options(-Wno-error=format= -Wno-format)