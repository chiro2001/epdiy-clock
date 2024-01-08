file(GLOB app_sources *.c)

idf_component_register(
  SRCS ${app_sources}
  INCLUDE_DIRS "include"
  REQUIRES 
    epdiy
    esp_jpeg
    esp_rom
    nvs_flash esp-tls esp_http_client
    esp_app_format
    esp_wifi
    pngle
  EMBED_TXTFILES ${project_dir}/ssl_cert/server_cert.pem
)

# make compliler happy
# idf_build_set_property(COMPILE_OPTIONS "-Wformat=0" APPEND)
component_compile_options(-Wno-error=format= -Wno-format)