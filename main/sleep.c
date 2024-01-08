#include "common.h"

void deepsleep() {
  printf("Go to sleep %d minutes\n", DEEPSLEEP_MINUTES_AFTER_RENDER);
  esp_deep_sleep(1000000LL * 60 * DEEPSLEEP_MINUTES_AFTER_RENDER);
}