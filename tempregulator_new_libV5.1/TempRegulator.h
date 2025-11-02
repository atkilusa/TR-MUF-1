#pragma once                                                              // Предотвращает повторное включение заголовка
//
#include <lvgl.h>                                                         // Основные определения LVGL (виджеты, события)
//
#include <array>                                                          // std::array для фиксированных наборов профилей
#include <vector>                                                         // Используем std::vector для хранения списков значений
//
#include "PIDController.h"                                               // Класс PID-регулятора
#include "TemperatureProfile.h"                                          // Температурные профили, загружаемые из NVS
#include "TouchCalibration.h"                                            // Общие определения калибровки тачскрина

class WebInterface;                                                       // Modified: предварительное объявление веб-интерфейса

#ifndef LVGL_VERSION_MAJOR
#if defined(LV_VERSION_MAJOR)
#define LVGL_VERSION_MAJOR LV_VERSION_MAJOR
#else
#define LVGL_VERSION_MAJOR 0
#endif
#endif

#if LVGL_VERSION_MAJOR >= 9
using SplashImageDescriptor = lv_image_dsc_t;                              // Тип дескриптора изображения для LVGL 9
#else
using SplashImageDescriptor = lv_img_dsc_t;                                // Тип дескриптора изображения для LVGL 8
#endif
//
enum State : uint8_t {                                                    // Перечисление состояний конечного автомата регулятора
  STATE_INIT = 0,                                                         // Начальная инициализация
  STATE_READY,                                                            // Готовность, главный экран
  STATE_WORK,                                                             // Работа по профилю/поддержание температуры
  STATE_SETTINGS,                                                         // Меню настроек
  STATE_CALIBRATE_SENSOR,                                                 // Мастер калибровки термопары
  STATE_AUTOTUNE_PID,                                                     // Автонастройка PID
  STATE_ALARM,                                                            // Экран аварии
  STATE_TOUCH_CALIBRATE,                                                  // Калибровка тачскрина
  STATE_TOUCH_TEST,                                                       // Тест тача
  STATE_MANUAL                                                            // Ручной режим
};                                                                        // Конец перечисления State
//
enum Event : uint8_t {                                                    // События, переводящие автомат между состояниями
  EVENT_NONE = 0,                                                         // Нет события
  EVENT_INIT_OK,                                                          // Инициализация прошла успешно
  EVENT_INIT_FAIL,                                                        // Ошибка инициализации
  EVENT_TO_SETTINGS,                                                      // Переход в настройки
  EVENT_TO_PROFILE_WORK,                                                  // Старт работы по профилю
  EVENT_TO_CALIB,                                                         // Запуск калибровки
  EVENT_TO_AUTOTUNE,                                                      // Запуск автонастройки PID
  EVENT_STOP,                                                             // Остановка процессов
  EVENT_TO_PROFILES,                                                      // Открыть список профилей
  EVENT_TO_MANUAL                                                         // Перейти в ручной режим
};                                                                        // Конец перечисления Event
//
enum CalibState : uint8_t {                                               // Состояния мастера калибровки термопары
  CAL_IDLE = 0,                                                           // Ожидание
  CAL_STEP1_INPUT_AMBIENT,                                                // Пользователь вводит температуру окружающей среды
  CAL_STEP1_MEASURE,                                                      // Считываем данные в первом шаге
  CAL_STEP2_WAIT_STABLE,                                                  // Ожидаем стабилизацию второй точки
  CAL_STEP2_MEASURE,                                                      // Измеряем вторую точку
  CALC_COEFFS,                                                            // Расчёт коэффициентов
  CAL_DONE,                                                               // Калибровка завершена успешно
  CAL_ERROR                                                               // Ошибка калибровки
};                                                                        // Конец перечисления CalibState
//
enum AtState : uint8_t {                                                  // Состояния автомата автонастройки PID
  AT_IDLE = 0,                                                            // Простой
  AT_SETUP_TARGET,                                                        // Выбор целевой температуры
  AT_CONFIRM,                                                             // Подтверждение запуска
  AT_RUNNING,                                                             // Выполнение алгоритма автонастройки
  AT_DONE,                                                                // Завершение с успешными коэффициентами
  AT_ABORT,                                                               // Принудительное прерывание
  AT_ERROR                                                                // Ошибка автонастройки
};                                                                        // Конец перечисления AtState
//
enum TouchCalStep : uint8_t { TCS_IDLE = 0, TCS_1, TCS_2, TCS_3, TCS_4, TCS_DONE };  // Этапы калибровки тача по четырём точкам
//
class TempRegulator {                                                     // Главный класс, управляющий логикой устройства
  friend class WebInterface;                                              // Modified: разрешаем веб-интерфейсу доступ к приватным методам
public:                                                                   // Публичные методы
  void begin();                                                           // Инициализация всех подсистем
  void update();                                                          // Главный тик: обработка состояния и UI
//
  void do_reset_touch();                                                  // Сброс параметров тачскрина к заводским
  void do_reset_tc();                                                     // Сброс калибровки термопары
  void do_reset_pid();                                                    // Сброс коэффициентов PID
  void openInfoDialog();                                                  // Открыть информационное окно о версии/состоянии
//
  Event ev = EVENT_NONE;                                                  // Текущее событие для автомата
  bool  isCalibrated = false;                                             // Признак пройденной калибровки датчика
//
  void createMain();                                                      // Создать главный экран
  void createProfiles();                                                  // Создать экран выбора профилей
  void createSettings();                                                  // Создать меню настроек
  void createAdvancedSettings();                                          // Создать меню продвинутых настроек
  void createPidCoeffsMenu();                                             // Редактор коэффициентов PID
  void createThermoCoeffsMenu();                                          // Редактор коэффициентов термопары
  void createResetMenu();                                                 // Создать экран сбросов
  void adjustPidCoeffByIndex(int idx, double delta);                      // Изменить коэффициент PID (UI)
  void adjustThermoCoeffByIndex(int idx, float delta);                    // Изменить коэффициент термопары (UI)
  void resetPidCoeffsToDefaults();                                        // Сброс коэффициентов PID (UI)
  void resetThermoCoeffsToDefaults();                                     // Сброс коэффициентов термопары (UI)
  void createWork();                                                      // Создать экран работы
  void createCalibS1();                                                   // Создать экран первого шага калибровки
  void createCalibS2();                                                   // Создать экран второго шага калибровки
  void createCalibMsg(const char* text);                                  // Показать информационное сообщение при калибровке
  void createAtSetup();                                                   // Создать экран выбора цели автонастройки
  void createAtConfirm();                                                 // Создать экран подтверждения автонастройки
  void createAtRun();                                                     // Создать экран хода автонастройки
  void createTouchCalib();                                                // Создать экран калибровки тача
  void createTouchTest();                                                 // Создать экран теста тача
  void createSplash();                                                    // Создать заставку при старте
//
  void createManual();                                                    // Создать интерфейс ручного режима
  void onEnterManual();                                                   // Логика при входе в ручной режим
//
  void handleProfileSelection(lv_obj_t* target);                          // Обработка нажатия на профиль
  float getLastTemperatureC() const { return lastTemperatureC; }          // Modified: последняя измеренная температура
  int getActiveProfileIndex() const {                                      // Modified: индекс активного профиля (1..10)
    return (activeProfileIndex >= 0) ? (activeProfileIndex + 1) : 0;
  }
  String describeStateForWeb() const;                                      // Modified: текстовое описание состояния
//
  lv_obj_t* lbl_man_cur = nullptr;                                        // Указатель на метку текущей температуры в ручном режиме
  lv_obj_t* lbl_man_sp = nullptr;                                         // Метка заданной температуры в ручном режиме
//
  lv_obj_t* lbl_work_cur = nullptr;                                       // Метка текущей температуры на рабочем экране
  lv_obj_t* lbl_work_sp = nullptr;                                        // Метка заданной температуры на рабочем экране
  lv_obj_t* lbl_work_pow = nullptr;                                       // Метка мощности нагрева на рабочем экране
//
  lv_obj_t* lbl_cal_val = nullptr;                                        // Метка значения АЦП в процессе калибровки
  lv_obj_t* btn_ok = nullptr;                                             // Кнопка подтверждения в диалогах калибровки
  lv_obj_t* lbl_pid_kp_val = nullptr;                                     // Значение коэффициента Kp в UI
  lv_obj_t* lbl_pid_ki_val = nullptr;                                     // Значение коэффициента Ki в UI
  lv_obj_t* lbl_pid_kd_val = nullptr;                                     // Значение коэффициента Kd в UI
  lv_obj_t* lbl_tc_kl_val = nullptr;                                      // Значение коэффициента Kl в UI
  lv_obj_t* lbl_tc_kc_val = nullptr;                                      // Значение коэффициента Kc в UI
//
  CalibState cst = CAL_IDLE;                                              // Текущее состояние мастера калибровки
  float     cal_temp1 = 25.0f;                                            // Первая эталонная температура
  float     cal_temp2 = 100.0f;                                           // Вторая эталонная температура
  uint32_t  cal_t0 = 0;                                                   // Таймстамп начала шага калибровки
  uint32_t  stable_t0 = 0;                                                // Таймстамп начала стабильного интервала
  bool      stable = false;                                               // Флаг устойчивости показаний
  uint16_t  cal_adc1 = 0;                                                 // Значение АЦП в первой точке
  uint16_t  cal_adc2 = 0;                                                 // Значение АЦП во второй точке
//
  bool hasAlarm() const { return alarm_active; }                          // Проверка активной аварии
  void clearAlarm();                                                      // Сброс аварийного состояния
//
  bool heating = true;                                                    // Признак разрешения нагрева
  void startHeat();                                                       // Включить нагрев и интерфейсные элементы
  void stopHeat();                                                        // Отключить нагрев
  void setHeating(bool on);                                               // Установить состояние нагрева
  void updateHeatButtonsUI();                                             // Обновить кнопки нагрева в UI
  lv_obj_t* btn_work_heat = nullptr;                                      // Кнопка нагрева на рабочем экране
  lv_obj_t* btn_manual_heat = nullptr;                                    // Кнопка нагрева в ручном режиме
//
  AtState atst = AT_IDLE;                                                 // Состояние автомата автонастройки
  float   at_target = 190.0f;                                             // Целевая температура для автонастройки
//
  void onEnterReady();                                                    // Действия при входе в состояние READY
  void onEnterSettings();                                                 // При входе в настройки
  void onEnterWork();                                                     // При переходе в рабочий режим
  void onEnterCalib();                                                    // При запуске калибровки
  void onEnterAutotune();                                                 // При запуске автонастройки
  void onEnterAlarm(const char* text);                                    // При появлении аварии
  void onEnterTouchCalib();                                               // При запуске калибровки тача
  void onEnterTouchTest();                                                // При запуске теста тача
//
  lv_obj_t* msgbox(const char* text, const char* btn1 = "ОК");           // Универсальная функция создания сообщений
//
  float getTargetC() const;                                               // Получить текущую уставку
  void  setTargetC(float c);                                              // Задать уставку
  void  adjustTargetC(float delta);                                       // Изменить уставку на указанную величину
//
  void startAutotune();                                                   // Запустить процедуру автонастройки PID
//
private:                                                                  // Приватные поля и методы
  State state = STATE_INIT;                                               // Текущее состояние автомата
//
  float   slope = 1.0f;                                                   // Коэффициент преобразования измерений
  float   offset = 0.0f;                                                  // Смещение измерений
  uint8_t consecutive_outlier_cycles = 0;                                 // Количество подряд обнаруженных выбросов датчика
  bool    alarm_active = false;                                           // Признак активной аварии
//
  PIDController pid;                                                      // Встроенный PID-регулятор
  double pid_kp = 2.0;                                                    // Текущий коэффициент P
  double pid_ki = 5.0;                                                    // Текущий коэффициент I
  double pid_kd = 1.0;                                                    // Текущий коэффициент D
  float  targetC = 210.0f;                                                // Заданная температура по умолчанию
  int    ssr_power_0_255 = 0;                                             // Мощность нагрева в диапазоне 0-255
  uint32_t ssr_window_start = 0;                                          // Время начала текущего окна ШИМ SSR
  float    lastTemperatureC = 0.0f;                                       // Modified: последняя измеренная температура

  std::array<TemperatureProfile, kTemperatureProfileCount> profiles{};   // Профили, считанные из NVS
  lv_obj_t* profileButtons[kTemperatureProfileCount]{};                  // Кнопки профилей на экране списка
  uint8_t  profileButtonToIndex[kTemperatureProfileCount]{};             // Соответствие кнопок индексам профилей
  size_t   profileButtonCount = 0;                                       // Количество отображаемых профилей
  int8_t   activeProfileIndex = -1;                                      // Текущий выбранный профиль
//
  lv_obj_t* scr_main = nullptr;                                           // Экран главного меню
  lv_obj_t* scr_settings = nullptr;                                       // Экран настроек
  lv_obj_t* scr_work = nullptr;                                           // Рабочий экран
  lv_obj_t* scr_profiles = nullptr;                                       // Экран профилей
  lv_obj_t* scr_cal_s1 = nullptr;                                         // Экран первого шага калибровки
  lv_obj_t* scr_cal_s2 = nullptr;                                         // Экран второго шага калибровки
  lv_obj_t* scr_cal_msg = nullptr;                                        // Сообщение для состояния калибровки

  bool loadSplashImage();                                                 // Загрузить заставку из LittleFS
  SplashImageDescriptor splash_img_dsc{};                                  // Описание изображения заставки
  std::vector<uint8_t> splash_img_buf;                                     // Буфер пикселей заставки (RGB565)
//
  uint32_t at_t0 = 0;                                                     // Время начала автонастройки
  bool     relay_on = false;                                              // Текущее состояние реле/SSR
  float    relay_hyst = 2.0f;                                             // Гистерезис для управления нагревом
  std::vector<uint32_t> peak_ts;                                          // Таймстампы обнаруженных пиков температуры
  std::vector<float>    peak_val;                                         // Значения температурных пиков
  bool was_above = false;                                                 // Флаг, что температура была выше цели
  lv_obj_t* scr_at_setup = nullptr;                                       // Экран настройки автонастройки
  lv_obj_t* scr_at_confirm = nullptr;                                     // Экран подтверждения
  lv_obj_t* scr_at_run = nullptr;                                         // Экран выполнения
  lv_obj_t* lbl_at_cur = nullptr;                                         // Метка текущей температуры при автонастройке
  lv_obj_t* lbl_at_time = nullptr;                                        // Метка времени при автонастройке
//
  TouchCalStep tcs = TCS_IDLE;                                            // Этап калибровки тачскрина
  lv_obj_t* scr_tcal = nullptr;                                           // Экран калибровки тача
  lv_obj_t* lbl_tcal = nullptr;                                           // Текстовые подсказки калибровки
  lv_obj_t* cross = nullptr;                                              // Объект перекрестия для касаний
  uint16_t rawx[4]{};                                                     // Сырые координаты X по четырём точкам
  uint16_t rawy[4]{};                                                     // Сырые координаты Y
  uint8_t  tcal_idx = 0;                                                  // Индекс текущей точки калибровки
  bool     t_pressed = false;                                             // Флаг факта касания
  uint32_t t_press_t0 = 0;                                                // Время начала касания
  lv_obj_t* scr_ttest = nullptr;                                          // Экран теста тачскрина
//
  void saveNVS();                                                         // Сохранение конфигурации в хранилище
  bool loadNVS();                                                         // Загрузка конфигурации из хранилища
//
  uint16_t readAdcFiltered(uint8_t& out_outliers);                        // Чтение АЦП с фильтрацией выбросов
  float    readTemperatureC();                                            // Расчёт температуры в градусах Цельсия
  void     beep(uint16_t ms = 50);                                        // Короткий звуковой сигнал
  void     ssrApply();                                                    // Применение вычисленной мощности к SSR
//
  static void cb_reset_touch(void* user);                                 // Обработчик кнопки сброса тача
  static void cb_reset_tc(void* user);                                    // Обработчик кнопки сброса термопары
  static void cb_reset_pid(void* user);                                   // Обработчик кнопки сброса PID
//
  void startCalibration();                                                // Запуск процесса калибровки термопары
  void tickCalibration();                                                 // Один шаг логики калибровки
  void updateCalibStableUI(bool st);                                      // Обновление UI статуса стабильности
  bool isCalibTimedOut() const;                                           // Проверка таймаута калибровки
  void saveCalibration(float off, float sl);                              // Сохранение вычисленных коэффициентов
//
  void tickAutotune();                                                    // Шаг алгоритма автонастройки
  void finishAutotune(double kp, double ki, double kd);                   // Завершение автонастройки с сохранением коэффициентов
//
  void tickTouchCalib();                                                  // Обработка шага калибровки тача
  void tcal_next_target();                                                // Перейти к следующей точке
  void tcal_finish_and_save();                                            // Завершить калибровку и сохранить результат
//
  void refreshPidCoeffLabels();                                           // Обновить отображение PID коэффициентов
  void refreshThermoCoeffLabels();                                        // Обновить отображение коэффициентов термопары
//
  void loadTemperatureProfiles();                                         // Загрузка профилей из NVS и их подготовка
//
  void clearNVS();                                                        // Полный сброс сохранённых данных
};                                                                        // Конец определения класса TempRegulator
//
extern TempRegulator regulator;                                           // Глобальный экземпляр регулятора, доступный во всём проекте

