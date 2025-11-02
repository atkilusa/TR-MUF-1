#pragma once                                                     // Защищаем заголовок от повторного включения
//
#include <lvgl.h>                                                // Заголовок LVGL с определением lv_indev_t
//
namespace EncoderInput {                                         // Пространство имён для функций работы с энкодером
//
void setupHardware();                                            // Настройка пинов и прерываний энкодера
void createInputDevice();                                        // Регистрация устройства ввода в LVGL
lv_indev_t* getInputDevice();                                    // Получение указателя на созданное устройство ввода
//
}  // namespace EncoderInput                                     // Завершение пространства имён

