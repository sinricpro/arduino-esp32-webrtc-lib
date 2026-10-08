// frame2jpg() on every target; see esp_camera.h.
#pragma once
#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32P4
#include "p4_camera/img_converters_p4.h"
#else
#include_next <img_converters.h>
#endif
