#include "Storage.h"                                                               // Заголовок с описанием структуры конфигурации и API хранения
//
#include <Arduino.h>                                                               // Для работы со строками String и общими типами Arduino
#include <LittleFS.h>                                                              // Файловая система LittleFS на ESP32
#include <stdlib.h>                                                                // Функции strtol/strtoul/strtod
#include <string.h>                                                                // Функция strlen
//
namespace {                                                                        // Локальные константы и функции, не видимые за пределами файла
constexpr const char* kConfigPath = "/config.ini";                               // Путь к файлу конфигурации в LittleFS
constexpr uint32_t    kConfigVersion = 1;                                         // Версия формата файла конфигурации
//
String readValue(const String& line, const char* key) {                           // Возвращает часть строки после указанного префикса
  const size_t key_len = strlen(key);                                             // Длина ключа (включая знак '=')
  if (!line.startsWith(key)) {                                                    // Если строка не начинается с ключа
    return String();                                                              // Возвращаем пустую строку (нет совпадения)
  }                                                                               // Конец проверки префикса
  return line.substring(key_len);                                                 // Возвращаем всё, что стоит после ключа
}                                                                                 // Завершение вспомогательной функции readValue
//
bool parseBool(const String& line, const char* key, bool& out) {                  // Парсинг булевого значения вида key=0/1
  String value = readValue(line, key);                                            // Выделяем значение после ключа
  if (value.length() == 0) {                                                      // Если ключ не совпал
    return false;                                                                 // Сообщаем, что парсинг не произошёл
  }                                                                               // Конец проверки
  value.trim();                                                                   // Удаляем пробелы и символы перевода строки
  out = (value.toInt() != 0);                                                     // Любое ненулевое значение трактуем как true
  return true;                                                                    // Успешный парсинг
}                                                                                 // Завершение parseBool
//
bool parseUInt16(const String& line, const char* key, uint16_t& out) {             // Парсинг 16-битного беззнакового целого
  String value = readValue(line, key);                                            // Извлекаем значение по ключу
  if (value.length() == 0) {                                                      // Если ключ не найден
    return false;                                                                 // Выходим
  }                                                                               // Конец проверки
  value.trim();                                                                   // Очищаем строку от пробелов
  long v = strtol(value.c_str(), nullptr, 10);                                    // Преобразуем в long, учитывая возможный знак
  if (v < 0) {                                                                    // Если число отрицательное
    v = 0;                                                                        // Ограничиваем нижним пределом
  }                                                                               // Конец проверки нижнего предела
  if (v > 0xFFFF) {                                                               // Если число превышает максимально допустимое значение
    v = 0xFFFF;                                                                   // Ограничиваем верхним пределом 16-битного значения
  }                                                                               // Конец проверки верхнего предела
  out = static_cast<uint16_t>(v);                                                 // Сохраняем результат в выходной параметр
  return true;                                                                    // Сообщаем об успешном парсинге
}                                                                                 // Завершение parseUInt16
//
bool parseFloat(const String& line, const char* key, float& out) {                 // Парсинг числа с плавающей точкой одинарной точности
  String value = readValue(line, key);                                            // Извлекаем значение по ключу
  if (value.length() == 0) {                                                      // Если ключ не найден
    return false;                                                                 // Выходим
  }                                                                               // Конец проверки
  value.trim();                                                                   // Удаляем лишние пробелы
  out = value.toFloat();                                                          // Конвертируем строку в float при помощи Arduino API
  return true;                                                                    // Возвращаем успех
}                                                                                 // Завершение parseFloat
//
bool parseDouble(const String& line, const char* key, double& out) {               // Парсинг числа двойной точности
  String value = readValue(line, key);                                            // Извлекаем значение по ключу
  if (value.length() == 0) {                                                      // Если ключ отсутствует в строке
    return false;                                                                 // Сообщаем об этом вызывающему коду
  }                                                                               // Конец проверки
  value.trim();                                                                   // Убираем пробелы
  out = strtod(value.c_str(), nullptr);                                           // Используем стандартную функцию для преобразования в double
  return true;                                                                    // Успешное завершение
}                                                                                 // Завершение parseDouble
//
}  // namespace                                                                    // Завершение анонимного пространства имён
//
namespace Storage {                                                               // Основное пространство имён модуля хранения
//
bool begin() {                                                                    // Инициализация файловой системы LittleFS
  if (LittleFS.begin(true)) {                                                     // Пытаемся смонтировать файловую систему (true разрешает форматирование при ошибке)
    return true;                                                                  // Если успешно, возвращаем true
  }                                                                               // Конец проверки
  return false;                                                                   // Иначе сообщаем о неудаче
}                                                                                 // Завершение begin
//
bool load(PersistentConfig& out) {                                                // Загружаем конфигурацию из файла в структуру
  File f = LittleFS.open(kConfigPath, FILE_READ);                                 // Открываем файл на чтение
  if (!f) {                                                                       // Если открыть не удалось
    return false;                                                                 // Сообщаем об ошибке
  }                                                                               // Конец проверки открытия
//
  uint32_t version = 0;                                                           // Переменная для хранения версии из файла
  PersistentConfig tmp{};                                                         // Временная структура, заполняем её до проверки версии
  tmp.calibrated        = false;                                                  // Значение по умолчанию: термопара не откалибрована
  tmp.offset            = 0.0f;                                                   // Сдвиг температуры по умолчанию
  tmp.slope             = 1.0f;                                                   // Множитель (наклон) по умолчанию
  tmp.pid_kp            = 2.0;                                                    // Начальные коэффициенты PID
  tmp.pid_ki            = 5.0;
  tmp.pid_kd            = 1.0;
  tmp.touch_calibrated  = false;                                                  // Тач по умолчанию не калиброван
  tmp.touch_swap        = false;                                                  // Оси не меняем
  tmp.touch_tx_min      = 300;                                                    // Базовые границы тача по X
  tmp.touch_tx_max      = 3900;
  tmp.touch_ty_min      = 200;                                                    // Базовые границы тача по Y
  tmp.touch_ty_max      = 3900;
//
  while (f.available()) {                                                         // Читаем файл построчно до конца
    String line = f.readStringUntil('\n');                                       // Получаем строку до символа новой строки
    line.trim();                                                                  // Убираем пробелы и \r
    if (line.length() == 0 || line.startsWith("#")) {                            // Пропускаем пустые строки и комментарии
      continue;                                                                   // Переходим к следующей строке
    }                                                                             // Конец проверки пустых строк
    if (line.startsWith("version=")) {                                           // Если это строка с версией
      String v = readValue(line, "version=");                                   // Выделяем значение версии
      v.trim();                                                                   // Убираем пробелы
      version = static_cast<uint32_t>(strtoul(v.c_str(), nullptr, 10));           // Преобразуем к числу
    } else if (parseBool(line, "calibrated=", tmp.calibrated)) {                 // Парсим булевые и числовые ключи по очереди
      continue;                                                                   // Если успешно, переходим к следующей строке
    } else if (parseFloat(line, "offset=", tmp.offset)) {
      continue;
    } else if (parseFloat(line, "slope=", tmp.slope)) {
      continue;
    } else if (parseDouble(line, "kp=", tmp.pid_kp)) {
      continue;
    } else if (parseDouble(line, "ki=", tmp.pid_ki)) {
      continue;
    } else if (parseDouble(line, "kd=", tmp.pid_kd)) {
      continue;
    } else if (parseBool(line, "touch_calibrated=", tmp.touch_calibrated)) {
      continue;
    } else if (parseBool(line, "touch_swap=", tmp.touch_swap)) {
      continue;
    } else if (parseUInt16(line, "touch_tx_min=", tmp.touch_tx_min)) {
      continue;
    } else if (parseUInt16(line, "touch_tx_max=", tmp.touch_tx_max)) {
      continue;
    } else if (parseUInt16(line, "touch_ty_min=", tmp.touch_ty_min)) {
      continue;
    } else if (parseUInt16(line, "touch_ty_max=", tmp.touch_ty_max)) {
      continue;
    }
  }                                                                               // Конец чтения файла
//
  f.close();                                                                      // Закрываем файл после чтения
  if (version != kConfigVersion) {                                                // Проверяем совпадение версии конфигурации
    return false;                                                                 // Если версия не совпадает — сигнализируем об ошибке
  }                                                                               // Конец проверки версии
//
  out = tmp;                                                                      // Копируем временную структуру в выходной параметр
  return true;                                                                    // Сообщаем об успешной загрузке
}                                                                                 // Завершение функции load
//
bool save(const PersistentConfig& data) {                                         // Сохраняем структуру конфигурации в файл
  LittleFS.remove(kConfigPath);                                                   // Удаляем предыдущий файл, если он был
  File f = LittleFS.open(kConfigPath, FILE_WRITE);                                // Создаём новый файл для записи
  if (!f) {                                                                       // Если открыть на запись не удалось
    return false;                                                                 // Возвращаем ошибку
  }                                                                               // Конец проверки открытия
//
  f.printf("# AKTex TempRegulator persistent configuration\n");                  // Записываем комментарий-заголовок
  f.printf("version=%lu\n", static_cast<unsigned long>(kConfigVersion));         // Сохраняем текущую версию формата
  f.printf("calibrated=%d\n", data.calibrated ? 1 : 0);                          // Сохраняем флаг калибровки термопары
  f.printf("offset=%.5f\n", static_cast<double>(data.offset));                  // Сохраняем смещение термопары
  f.printf("slope=%.5f\n", static_cast<double>(data.slope));                    // Сохраняем коэффициент наклона
  f.printf("kp=%.6f\n", data.pid_kp);                                           // Коэффициент P
  f.printf("ki=%.6f\n", data.pid_ki);                                           // Коэффициент I
  f.printf("kd=%.6f\n", data.pid_kd);                                           // Коэффициент D
  f.printf("touch_calibrated=%d\n", data.touch_calibrated ? 1 : 0);             // Флаг калибровки тача
  f.printf("touch_swap=%d\n", data.touch_swap ? 1 : 0);                         // Флаг перестановки осей тача
  f.printf("touch_tx_min=%u\n", static_cast<unsigned>(data.touch_tx_min));     // Минимальное значение X
  f.printf("touch_tx_max=%u\n", static_cast<unsigned>(data.touch_tx_max));     // Максимальное значение X
  f.printf("touch_ty_min=%u\n", static_cast<unsigned>(data.touch_ty_min));     // Минимальное значение Y
  f.printf("touch_ty_max=%u\n", static_cast<unsigned>(data.touch_ty_max));     // Максимальное значение Y
  f.close();                                                                      // Закрываем файл после записи
  return true;                                                                    // Возвращаем успех
}                                                                                 // Завершение функции save
//
bool clear() {                                                                    // Удаляем файл конфигурации
  if (LittleFS.exists(kConfigPath)) {                                             // Проверяем, существует ли файл
    return LittleFS.remove(kConfigPath);                                          // Удаляем и возвращаем результат операции
  }                                                                               // Конец проверки существования
  return true;                                                                    // Если файла не было — считаем, что всё хорошо
}                                                                                 // Завершение функции clear
//
}  // namespace Storage                                                           // Конец пространства имён Storage

