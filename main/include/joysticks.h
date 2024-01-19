#ifndef __JOYSTICKS_H__
#define __JOYSTICKS_H__

#include "common.h"
#include "esp_adc/adc_oneshot.h"

typedef struct {
    bool up;
    bool down;
    bool left;
    bool right;
    bool click;
} virtual_buttons_t;

void task_joysticks(void *args);

#endif // __JOYSTICKS_H__