#pragma once                                                              // Modified: предотвращаем повторное включение

#include <Arduino.h>                                                      // Modified: базовые типы Arduino
#include <ArduinoJson.h>                                                  // Modified: структуры JSON
#include <ESPAsyncWebServer.h>                                            // Modified: HTTP-сервер
#include <WebSocketsServer.h>                                             // Modified: WebSocket сервер

class TempRegulator;                                                      // Modified: вперёд объявляем главный класс

class WebInterface {                                                      // Modified: оболочка для работы с веб-интерфейсом
public:
  static WebInterface& instance();                                        // Modified: получение singleton

  void begin(TempRegulator* regulator);                                   // Modified: запускаем веб-сервер и WS
  void loop();                                                            // Modified: вызываем в главном цикле
  void updateTelemetry(const TempRegulator& regulator);                   // Modified: передаём актуальные данные регулятора

  void setProfileAlarm(bool active, const String& message);               // Modified: сигнализация по профилю
  void setRegulatorAlarm(bool active, const String& message);             // Modified: сигнализация по регулятору
  void noteProfileStart(uint32_t ms);                                     // Modified: отметка старта профиля
  void noteProfileStop();                                                 // Modified: отметка остановки профиля

private:
  WebInterface();                                                         // Modified: закрытый конструктор

  static void handleWebSocketEvent(uint8_t client_num,                    // Modified: статический обработчик WS
                                   WStype_t type,
                                   uint8_t* payload,
                                   size_t length);

  void processInitRequest();                                              // Modified: отсылаем список профилей и настроек
  void processSaveRequest(const JsonDocument& doc);                       // Modified: сохраняем профиль из веба
  void processDeleteRequest(const JsonDocument& doc);                     // Modified: удаляем профиль
  void processSettingsRequest(const JsonDocument& doc);                   // Modified: сохраняем настройки
  void processDebugFlags(const JsonDocument& doc);                        // Modified: обновляем отладочные флаги

  void broadcastTelemetry();                                              // Modified: собираем и отправляем телеметрию
  String buildDiffMessage();                                              // Modified: формируем JSON с изменениями

  TempRegulator* regulator_ = nullptr;                                    // Modified: ссылка на регулятор
  AsyncWebServer server_{80};                                             // Modified: HTTP-сервер для статики
  WebSocketsServer socket_{1337};                                         // Modified: WebSocket сервер

  bool profisAlarm_ = false;                                              // Modified: текущее состояние тревоги профиля
  bool newProfisAlarm_ = false;                                           // Modified: новое состояние тревоги профиля
  bool regisAlarm_ = false;                                               // Modified: текущее состояние тревоги регулятора
  bool newRegisAlarm_ = false;                                            // Modified: новое состояние тревоги регулятора
  String errValRegisAlarm_ =                                             // Modified: текст ошибки регулятора
      "\xD0\xA2\xD0\xB5\xD1\x81\xD1\x82\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x8F \xD0\xBE\xD1\x88\xD0\xB8\xD0\xB1\xD0\xBA\xD0\xB0";

  String timestartprofil_;                                               // Modified: сохранённое время старта профиля
  String newTimestartprofil_;                                            // Modified: новое время старта профиля
  String timestopprofil_;                                                // Modified: сохранённое время остановки профиля
  String newTimestopprofil_;                                             // Modified: новое время остановки профиля
  String timestartstupen_;                                               // Modified: старт ступени
  String newTimestartstupen_;                                            // Modified: новый старт ступени
  String timestopstupen_;                                                // Modified: стоп ступени
  String newTimestopstupen_;                                             // Modified: новый стоп ступени
  String nstupen_;                                                       // Modified: текущая ступень
  String newNstupen_;                                                    // Modified: новая ступень
  String activprof_;                                                     // Modified: активный профиль
  String newActivprof_;                                                  // Modified: новый активный профиль
  String seltemp_;                                                       // Modified: целевая температура
  String newSeltemp_;                                                    // Modified: новая целевая температура
  String stateprofil_;                                                   // Modified: состояние профиля
  String newStateprofil_;                                                // Modified: новое состояние профиля

  float actualTempC_ = 0.0f;                                              // Modified: текущая измеренная температура
  float newActualTempC_ = 0.0f;                                           // Modified: новое измеренное значение

  static WebInterface* self_;                                            // Modified: указатель на singleton
};

