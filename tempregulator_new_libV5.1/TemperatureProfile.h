#pragma once  // предотвращаем повторное включение

#include <Arduino.h>
#include <ArduinoJson.h>

inline constexpr size_t kTemperatureProfileCount = 10;  // Modified: общее число профилей, резервируемых в NVS

// Одна строка температурного профиля (ступень/сегмент)
struct TempProfileRow {
  float rStartTemperature = 0.0f;  // начальная температура ступени, °C
  float rEndTemperature   = 0.0f;  // конечная   температура ступени, °C
  float rTime             = 0.0f;  // длительность ступени, мин (или сек — по вашей логике)
};

class TemperatureProfile {
public:
  // --- Константы ---
  static constexpr int MAX_ROWS = 10;

  // --- Конструкторы ---
  TemperatureProfile();
  TemperatureProfile(const String& ns, const String& defaultName = "");

  // --- Настройка пространства NVS и имени ---
  void setNamespace(const String& ns);
  void setDefaultName(const String& name);

  // --- Работа с NVS ---
  bool loadFromNVS();   // загрузить текущее состояние профиля из NVS
  bool UpdateFromNVS(); // синоним для удобства, вызывает loadFromNVS()

  // Сохранить профиль в NVS (имя, таблица строк, видимость для WEB/UI)
  // newRows может быть nullptr; rowCount — реальное число строк newRows
  bool saveToNVS(const String& name,
                 const TempProfileRow* newRows,
                 size_t rowCount,
                 bool visibleForWeb);

  // Очистить профиль в NVS (обнулить имя/видимость/строки)
  bool clearInNVS();

  // --- Экспорт/утилиты ---
  bool exportToJson(JsonDocument& doc) const;  // сериализация профиля в JSON
  bool hasPidCoefficients() const;             // есть ли ненулевые PID
  const TempProfileRow& step(size_t idx) const;// доступ к строке профиля
  void resetRows();                             // локально обнулить строки

  bool isAvailable() const;                     // Modified: флаг готовности профиля
  const String& name() const;                   // Modified: текущее имя профиля
  double kp() const;                            // Modified: коэффициент PID Kp
  double ki() const;                            // Modified: коэффициент PID Ki
  double kd() const;                            // Modified: коэффициент PID Kd
  int stepCount() const;                        // Modified: число заполненных ступеней

  // --- Публичные поля/состояние (чтобы регулятор мог быстро читать) ---
  String sNVSnamespace;   // имя пространства NVS, где хранится профиль
  String sNameProfile;    // отображаемое имя профиля

  // PID/калибровка термопары (если используются совместно с профилем)
  double rKp_PWM = 0.0;
  double rKi_PWM = 0.0;
  double rKd_PWM = 0.0;
  double rKl_TC  = 1.0;  // коэффициент наклона/градуировки
  double rKc_TC  = 0.0;  // смещение, °C

  bool  available       = false; // профиль пригоден для локального UI
  bool  availableForWeb = false; // отображать в веб-интерфейсе
  bool  showInMenu      = false; // отображать в локальном меню/списке
  int   usedRows        = 0;     // количество непустых строк

  TempProfileRow rows[MAX_ROWS]; // таблица ступеней профиля

private:
  // никаких приватных хелперов в .h не требуется — реализация в .cpp
};

// Создать (при необходимости) заготовки профилей по умолчанию в NVS.
// Вызывает writeProfileDefaults(...) внутри .cpp для пространств:
// "UserTmpProf_1" ... "UserTmpProf_10".
void ensureDefaultTemperatureProfiles();


