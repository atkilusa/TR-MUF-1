#include "GlobalPreferences.h"                                           // Modified: подключаем объявление глобального экземпляра

Preferences preferences;                                                  // Modified: создаём единую точку доступа к NVS
SemaphoreHandle_t preferencesMutex = xSemaphoreCreateRecursiveMutex();    // Modified: рекурсивный мьютекс для синхронизации

PreferencesLock::PreferencesLock() {                                      // Modified: конструктор захватывает мьютекс
  if (!preferencesMutex) {                                                // Modified: проверяем, создан ли мьютекс
    preferencesMutex = xSemaphoreCreateRecursiveMutex();                  // Modified: создаём повторно при необходимости
  }
  if (preferencesMutex &&                                                // Modified: захватываем, если мьютекс доступен
      xSemaphoreTakeRecursive(preferencesMutex, portMAX_DELAY) == pdPASS) {
    locked_ = true;                                                       // Modified: запоминаем успешный захват
  }
}

PreferencesLock::~PreferencesLock() {                                     // Modified: деструктор освобождает мьютекс
  if (locked_ && preferencesMutex) {                                      // Modified: проверяем, что держим мьютекс
    xSemaphoreGiveRecursive(preferencesMutex);                            // Modified: отдаём владение
  }
}

