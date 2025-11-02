#pragma once                                                              // Modified: общий заголовок для доступа к Preferences

#include <Preferences.h>                                                  // Modified: объявление класса Preferences из Arduino
#include <freertos/FreeRTOS.h>                                            // Modified: доступ к типам FreeRTOS
#include <freertos/semphr.h>                                              // Modified: используем рекурсивный мьютекс

extern Preferences preferences;                                           // Modified: единый экземпляр Preferences для всей прошивки
extern SemaphoreHandle_t preferencesMutex;                                // Modified: глобальный мьютекс для синхронизации доступа

class PreferencesLock {                                                   // Modified: RAII-обёртка для мьютекса
public:
  PreferencesLock();                                                      // Modified: захватываем мьютекс при создании
  ~PreferencesLock();                                                     // Modified: освобождаем мьютекс при разрушении

  PreferencesLock(const PreferencesLock&) = delete;                       // Modified: запрещаем копирование
  PreferencesLock& operator=(const PreferencesLock&) = delete;            // Modified: запрещаем присваивание

private:
  bool locked_ = false;                                                   // Modified: отслеживаем успешный захват
};

