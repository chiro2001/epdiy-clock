#include "common.h"

void deepsleep() {
  epd_deinit();
  esp_deep_sleep(1000000LL * 60 * DEEPSLEEP_MINUTES_AFTER_RENDER);
}