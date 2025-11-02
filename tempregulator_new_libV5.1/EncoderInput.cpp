#include "EncoderInput.h"                                                          // Подключаем заголовок с объявлениями для работы с энкодером
//
#include <Arduino.h>                                                               // Используем Arduino API для pinMode и attachInterrupt
#include "HardwareConfig.h"                                                       // Пины энкодера определены в конфигурации железа
#include "esp_timer.h"                                                            // Нужен для высокоточного таймера при антидребезге кнопки
#include "driver/gpio.h"                                                          // Низкоуровневый доступ к GPIO для чтения уровней в обработчиках прерываний
//
namespace EncoderInput {                                                           // Пространство имён с функциями управления энкодером
//
namespace {                                                                        // Внутреннее безымянное пространство имён для скрытых переменных
volatile int32_t encoder_diff = 0;                                                 // Накопленный инкремент вращения, обновляемый в прерываниях
volatile bool encoderButtonPressed = false;                                        // Флаг нажатия кнопки энкодера, выставляется в прерывании
lv_indev_t* encoderIndev = nullptr;                                                // Указатель на зарегистрированное устройство ввода LVGL
//
void IRAM_ATTR onEncoderTick() {                                                   // Обработчик прерывания по смене сигналов энкодера (вызывается из IRAM)
  static uint8_t last = 0xFF;                                                      // Храним предыдущее состояние линий A/B, 0xFF значит «не инициализировано»
  static int8_t accum = 0;                                                         // Накопление подшагов до полного щелчка
  uint8_t a = static_cast<uint8_t>(gpio_get_level((gpio_num_t)ENCODER_A_PIN));     // Читаем текущий уровень сигнала A
  uint8_t b = static_cast<uint8_t>(gpio_get_level((gpio_num_t)ENCODER_B_PIN));     // Читаем текущий уровень сигнала B
  uint8_t s = (a << 1) | b;                                                        // Объединяем уровни в двухбитовый код
  if (last == 0xFF) {                                                              // Если состояние ещё не инициализировано
    last = s;                                                                      // Запоминаем текущую комбинацию как стартовую
    return;                                                                        // Выходим, пока нет движения
  }                                                                                // Конец проверки первого вызова
  if ((last == 0b00 && s == 0b01) || (last == 0b01 && s == 0b11) ||                // Проверяем последовательность кодов на вращение вперёд
      (last == 0b11 && s == 0b10) || (last == 0b10 && s == 0b00)) {                // Возможные переходы при шаге вперёд
    if (accum < 0) {                                                               // Если до этого было движение в другую сторону — сбрасываем
      accum = 0;
    }
    accum++;                                                                       // Накопление подшагов вперёд
    if (accum >= 4) {                                                              // Полный щелчок (4 перехода квадратичного энкодера)
      encoder_diff++;                                                              // Увеличиваем счётчик шага
      accum = 0;                                                                   // Сбрасываем накопитель
    }
  } else if ((last == 0b00 && s == 0b10) || (last == 0b10 && s == 0b11) ||         // Иначе проверяем переходы при шаге назад
             (last == 0b11 && s == 0b01) || (last == 0b01 && s == 0b00)) {         // Все допустимые комбинации вращения обратно
    if (accum > 0) {                                                               // Смена направления — обнуляем накопитель
      accum = 0;
    }
    accum--;                                                                       // Накопление подшагов назад
    if (accum <= -4) {                                                             // Достигли полного шага в обратную сторону
      encoder_diff--;                                                              // Уменьшаем счётчик шага
      accum = 0;                                                                   // Обнуляем накопитель
    }
  }                                                                                // Конец определения направления
  last = s;                                                                        // Сохраняем текущий код для следующего сравнения
}                                                                                  // Завершение обработчика вращения
//
void IRAM_ATTR onEncoderButton() {                                                 // Обработчик прерывания кнопки энкодера
  static uint32_t last_press_us = 0;                                               // Время последнего подтверждённого нажатия для антидребезга
  constexpr uint32_t DEBOUNCE_US = 50000;                                          // 50 мс достаточно, чтобы отфильтровать дребезг контактов
  int level = gpio_get_level((gpio_num_t)ENCODER_BTN_PIN);                         // Читаем текущее состояние вывода кнопки
  if (level) {                                                                     // Фронт нажатия (уровень высокий)
    uint32_t now = static_cast<uint32_t>(esp_timer_get_time());                    // Текущее время в микросекундах
    if (now - last_press_us > DEBOUNCE_US) {                                       // Проверяем, что прошло больше времени, чем длительность дребезга
      encoderButtonPressed = true;                                                 // Фиксируем факт нажатия для основного цикла
      last_press_us = now;                                                         // Обновляем отметку времени последнего нажатия
    }
  }
}                                                                                  // Завершение обработчика кнопки
//
void encoder_read(lv_indev_t* indev, lv_indev_data_t* data) {                      // Колбэк LVGL для чтения состояния энкодера
  (void)indev;                                                                     // Указатель на устройство не используем, подавляем предупреждение
  int32_t diff = encoder_diff;                                                     // Сохраняем накопленное вращение локально
  encoder_diff = 0;                                                                // Обнуляем глобальный счётчик, чтобы не повторять шаги
  data->enc_diff = diff;                                                           // Передаём LVGL величину шага энкодера
  if (encoderButtonPressed) {                                                      // Если зарегистрировано новое нажатие
    data->state = LV_INDEV_STATE_PRESSED;                                          // Сообщаем LVGL о факте нажатия
    encoderButtonPressed = false;                                                  // Сразу сбрасываем флаг, чтобы на следующем чтении сообщить отпускание
  } else {                                                                         // Если нажатия не было
    data->state = LV_INDEV_STATE_RELEASED;                                         // Сообщаем LVGL, что кнопка отпущена
  }
}                                                                                  // Завершение функции чтения
//
}  // namespace                                                                     // Закрываем внутреннее пространство имён
//
void setupHardware() {                                                             // Настраиваем пины и прерывания энкодера
  pinMode(ENCODER_A_PIN, INPUT_PULLDOWN);                                          // Линия A: вход с подтяжкой вниз (активный высокий уровень)
  pinMode(ENCODER_B_PIN, INPUT_PULLDOWN);                                          // Линия B: вход с подтяжкой вниз
  pinMode(ENCODER_BTN_PIN, INPUT_PULLDOWN);                                        // Кнопка энкодера: подтяжка к земле, нажатием подаём «плюс»
  attachInterrupt(ENCODER_A_PIN, onEncoderTick, CHANGE);                           // Вызываем обработчик вращения при любой смене сигнала A
  attachInterrupt(ENCODER_B_PIN, onEncoderTick, CHANGE);                           // То же для сигнала B
  attachInterrupt(ENCODER_BTN_PIN, onEncoderButton, CHANGE);                       // Реагируем на оба фронта для отслеживания нажатия/отпускания
}                                                                                  // Завершение настройки железа
//
void createInputDevice() {                                                         // Регистрируем устройство ввода в LVGL
  if (encoderIndev) {                                                              // Если уже создано
    return;                                                                        // Ничего не делаем
  }                                                                                // Конец проверки существования
  encoderIndev = lv_indev_create();                                                // Создаём новое LVGL устройство ввода
  lv_indev_set_type(encoderIndev, LV_INDEV_TYPE_ENCODER);                          // Указываем тип «энкодер»
  lv_indev_set_read_cb(encoderIndev, encoder_read);                                // Назначаем функцию чтения состояния
}                                                                                  // Завершение создания устройства ввода
//
lv_indev_t* getInputDevice() { return encoderIndev; }                              // Возвращаем указатель на устройство ввода (может быть nullptr)
//
}  // namespace EncoderInput                                                        // Закрываем пространство имён EncoderInput

