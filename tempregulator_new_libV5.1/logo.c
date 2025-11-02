#include "LogoImageBuiltin.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t count;
    uint16_t value;
} LogoRun;

#include "LogoImageBuiltinData.inc"

#if LVGL_VERSION_MAJOR >= 9
typedef lv_image_dsc_t logo_image_dsc_t;
#else
typedef lv_img_dsc_t logo_image_dsc_t;
#endif

static logo_image_dsc_t g_logo_desc;
static uint8_t g_logo_pixels[(size_t)kBuiltinLogoWidth * (size_t)kBuiltinLogoHeight * 2u];
static bool g_logo_initialized = false;

static void logo_ensure_ready(void) {
    if (g_logo_initialized) {
        return;
    }

    const size_t expected = (size_t)kBuiltinLogoWidth * (size_t)kBuiltinLogoHeight * 2u;
    size_t offset = 0;
    for (size_t i = 0; i < kBuiltinLogoRleCount; ++i) {
        const LogoRun run = kBuiltinLogoRle[i];
        const uint16_t value = run.value;
        for (uint16_t j = 0; j < run.count; ++j) {
            if (offset + 1 >= expected) {
                break;
            }
            g_logo_pixels[offset++] = (uint8_t)(value & 0xFFu);
            g_logo_pixels[offset++] = (uint8_t)((value >> 8) & 0xFFu);
        }
        if (offset >= expected) {
            break;
        }
    }

    if (offset != expected) {
        // Failed to decode the expected number of pixels; mark as initialized to avoid loops.
        g_logo_desc.data = NULL;
        g_logo_desc.data_size = 0;
        g_logo_initialized = true;
        return;
    }

#if LVGL_VERSION_MAJOR >= 9
    g_logo_desc = (logo_image_dsc_t){0};
    g_logo_desc.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_logo_desc.header.cf = LV_COLOR_FORMAT_RGB565;
    g_logo_desc.header.flags = 0;
    g_logo_desc.header.w = kBuiltinLogoWidth;
    g_logo_desc.header.h = kBuiltinLogoHeight;
    g_logo_desc.header.stride = (uint32_t)kBuiltinLogoWidth * 2u;
    g_logo_desc.data_size = expected;
    g_logo_desc.data = g_logo_pixels;
#else
    g_logo_desc = (logo_image_dsc_t){0};
    g_logo_desc.header.always_zero = 0;
    g_logo_desc.header.cf = LV_IMG_CF_TRUE_COLOR;
    g_logo_desc.header.w = kBuiltinLogoWidth;
    g_logo_desc.header.h = kBuiltinLogoHeight;
    g_logo_desc.data_size = expected;
    g_logo_desc.data = g_logo_pixels;
#endif

    g_logo_initialized = true;
}

#if LVGL_VERSION_MAJOR >= 9
const lv_image_dsc_t* logo_builtin_get_image(void) {
#else
const lv_img_dsc_t* logo_builtin_get_image(void) {
#endif
    logo_ensure_ready();
    if (!g_logo_initialized || g_logo_desc.data == NULL) {
        return NULL;
    }
    return &g_logo_desc;
}

uint16_t logo_builtin_get_width(void) {
    return kBuiltinLogoWidth;
}

uint16_t logo_builtin_get_height(void) {
    return kBuiltinLogoHeight;
}

