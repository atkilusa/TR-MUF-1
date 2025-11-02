
/* lv_conf.h for LVGL v9 (Arduino) — Cyrillic-ready */

#ifndef LV_CONF_H
#define LV_CONF_H

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 16

/*====================
   FEATURE ENABLES
 *====================*/
#define LV_USE_DRAW_SW           1
#define LV_USE_LOG               0
#define LV_USE_ASSERT_HANDLER    0

/*====================
   TEXT / FONT
 *====================*/

/* If you use your own generated font (e.g., montserrat_16_cyr.c),
   declare it here so LVGL knows the symbol. */
#define LV_FONT_CUSTOM_DECLARE   LV_FONT_DECLARE(montserrat_16_cyr)

/* Set the default font for the whole UI to your custom one.
   Make sure `montserrat_16_cyr.c` is added to your sketch. */
#define LV_FONT_DEFAULT          (&montserrat_16_cyr)

/* Built-in Montserrat subset (not required when using custom font).
   Keep it off to reduce flash, or turn ON a size if you prefer. */
#define LV_USE_FONT_MONTSERRAT   0

#define LV_FONT_MONTSERRAT_16    1
#define LV_FONT_MONTSERRAT_24    1
#define LV_FONT_MONTSERRAT_26    1
#define LV_FONT_MONTSERRAT_40    1
#define LV_FONT_MONTSERRAT_46    1
#define LV_FONT_MONTSERRAT_28    1
#define LV_FONT_MONTSERRAT_32    1
#define LV_FONT_MONTSERRAT_12    1

/*====================
   MISC
 *====================*/
#define LV_MEM_SIZE (48U * 1024U)

/*====================
   LOGGER / ASSERT / PROFILER
 *====================*/
#define LV_USE_PROFILER 0

#endif /*LV_CONF_H*/
