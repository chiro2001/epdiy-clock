#ifndef __REQUEST_H__
#define __REQUEST_H__

#include "common.h"

esp_err_t https_get_request(esp_tls_cfg_t cfg, const char *url,
                                   const char *REQUEST, char **res_buf,
                                   uint32_t *ret_len);

#endif