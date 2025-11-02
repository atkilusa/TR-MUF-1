#pragma once                                                                // Предохраняет от повторного подключения заголовка
//
#include <stdint.h>                                                          // Определения целочисленных типов фиксированной ширины
//
extern bool g_touch_calibrated;                                             // Флаг завершённой калибровки тачскрина
extern bool g_touch_swap_axes;                                              // Флаг перестановки осей X и Y
extern uint16_t g_tx_min;                                                   // Минимальное значение X после калибровки
extern uint16_t g_tx_max;                                                   // Максимальное значение X
extern uint16_t g_ty_min;                                                   // Минимальное значение Y
extern uint16_t g_ty_max;                                                   // Максимальное значение Y
//
int16_t map_clamped(int32_t v, int32_t in_min, int32_t in_max,               // Преобразование значения с ограничением границ
                    int32_t out_min, int32_t out_max);                       // Возвращает координату в целевом диапазоне
void resetTouchCalibrationToDefaults();                                     // Сбрасывает параметры калибровки к значениям по умолчанию

