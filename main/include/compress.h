#ifndef __COMPRESS_H__
#define __COMPRESS_H__

#include "common.h"

esp_err_t compress_mem_to_file_zlib(const char *filename, const uint8_t *data,
                                    size_t length, int level);

esp_err_t decompress_file_to_mem_zlib(const char *filename, uint8_t *dest,
                                      size_t max_len);

esp_err_t compress_mem_to_file_miniz(const char *filename, const uint8_t *data,
                                     size_t length, int);

esp_err_t decompress_file_to_mem_miniz(const char *filename, uint8_t *dest,
                                       size_t max_len);

#endif