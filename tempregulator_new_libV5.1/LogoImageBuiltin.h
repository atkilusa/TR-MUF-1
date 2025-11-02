#pragma once

#include <lvgl.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if LVGL_VERSION_MAJOR >= 9
const lv_image_dsc_t* logo_builtin_get_image(void);
#else
const lv_img_dsc_t* logo_builtin_get_image(void);
#endif

uint16_t logo_builtin_get_width(void);
uint16_t logo_builtin_get_height(void);

#ifdef __cplusplus
}  // extern "C"
#endif

