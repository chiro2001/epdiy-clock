#ifndef __WIFI_H__
#define __WIFI_H__

#include "common.h"

void wifi_start_sta(void);
void wifi_stop_sta(void);
bool wifi_is_started(void);

#endif