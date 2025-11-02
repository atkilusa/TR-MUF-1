#pragma once                                           // Защищает заголовок от повторного включения
//
/* ========= PINS ========= */                        // Раздел с назначением пинов оборудования
#define PIN_SCLK   6                                   // SPI SCLK (тактовый вывод для дисплея и тача)
#define PIN_MOSI   7                                   // SPI MOSI (данные от ESP32)
#define PIN_MISO   2                                   // SPI MISO (данные к ESP32)
//
#define PIN_TFT_CS  10                                 // Пин выбора чипа TFT-дисплея
#define PIN_TFT_DC  4                                  // Пин выбора режима команд/данных дисплея
#define PIN_TFT_RST 5                                  // Пин аппаратного сброса TFT
//
#define PIN_TOUCH_CS  21                               // Пин выбора чипа тачскрина XPT2046
//
#define ENCODER_A_PIN   11                             // Линия A энкодера
#define ENCODER_B_PIN   3                              // Линия B энкодера
#define ENCODER_BTN_PIN 8                              // Кнопка энкодера
//
#define THERMOCOUPLE_PIN 1                             // Аналоговый вход усилителя термопары
#define SSR_CONTROL_PIN  19                            // Управление твердотельным реле нагревателя
#define SSR_FEEDBACK_PIN 20                            // Вход обратной связи SSR (если используется)
#define BUZZER_PIN       18                            // Пин пьезоизлучателя
//
#define LED_R_PIN 15                                   // Красный канал RGB-светодиода
#define LED_G_PIN 16                                   // Зелёный канал RGB-светодиода
#define LED_B_PIN 17                                   // Синий канал RGB-светодиода

