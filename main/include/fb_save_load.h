#ifndef __FB_SAVE_LOAD_H__
#define __FB_SAVE_LOAD_H__

#include "common.h"

esp_err_t fb_save_raw();
esp_err_t fb_save_compressed();
esp_err_t fb_load();
esp_err_t fb_load_compressed();

#endif