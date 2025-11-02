#pragma once                                               // Предотвращает множественное включение заголовка
//
#include <stdint.h>                                        // Определения целочисленных типов фиксированной ширины
//
struct PersistentConfig {                                  // Структура, описывающая сохраняемую конфигурацию устройства
  bool     calibrated;                                     // Флаг калибровки термопары
  float    offset;                                         // Смещение для корректировки измерений
  float    slope;                                          // Коэффициент наклона (масштаб)
  double   pid_kp;                                         // PID: пропорциональная часть
  double   pid_ki;                                         // PID: интегральная часть
  double   pid_kd;                                         // PID: дифференциальная часть
  bool     touch_calibrated;                               // Флаг калибровки тачскрина
  bool     touch_swap;                                     // Флаг перестановки осей тача
  uint16_t touch_tx_min;                                   // Минимальное значение X тача
  uint16_t touch_tx_max;                                   // Максимальное значение X тача
  uint16_t touch_ty_min;                                   // Минимальное значение Y тача
  uint16_t touch_ty_max;                                   // Максимальное значение Y тача
};                                                         // Завершение описания структуры
//
namespace Storage {                                        // Пространство имён с функциями работы с хранилищем
//
bool begin();                                              // Инициализация файловой системы
bool load(PersistentConfig& out);                          // Загрузка настроек из файла
bool save(const PersistentConfig& data);                   // Сохранение настроек в файл
bool clear();                                              // Удаление файла конфигурации
//
}  // namespace Storage                                    // Завершение пространства имён Storage

