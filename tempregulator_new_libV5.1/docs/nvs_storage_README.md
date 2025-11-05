# Документация по работе с NVS                                             <!-- Modified: описание механизма Preferences -->

## Общий экземпляр `Preferences`

* Файл [`GlobalPreferences.h`](../GlobalPreferences.h) объявляет глобальный
  объект `Preferences preferences`. Этот экземпляр используется во всех модулях
  прошивки, чтобы гарантировать единый доступ к NVS.
* Каждая операция с NVS начинается с `preferences.begin(namespace, readOnly)`
  и завершается `preferences.end()`. Это правило действует для всех модулей,
  чтобы не оставлять открытых пространств и избежать конфликтов доступа.

## Температурные профили

* Модуль [`TemperatureProfile.cpp`](../TemperatureProfile.cpp) отвечает за
  загрузку профилей через общий `preferences`. Каждому профилю соответствует
  пространство `UserTmpProf_N` (от 1 до 10).
* Метод `loadFromNVS()` загружает имя, флаги видимости, коэффициенты PID и
  таблицу шагов, проходя по ключам `rowX_rStartTemp`, `rowX_rEndTemp` и
  `rowX_rTime`.
* Запись и сброс профилей выполняются в [`WebInterface.cpp`](../WebInterface.cpp)
  через функцию `SaveProfileDataToNVS()`, которая обновляет ключи имени,
  флагов доступности и таблицу строк напрямую в выбранном пространстве.

## Настройки эмуляции/веб-интерфейса

* Веб-интерфейс хранит настройки (`activProf`, `isKalibrate`, `speedHot`,
  `tRoom`) в пространстве `Settings`. Запись выполняется через функцию
  `saveWebSettings()` внутри [`WebInterface.cpp`](../WebInterface.cpp).
* При инициализации фронта вызывается `EmulSettingsToJSON()`, которая читает
  значения из NVS и формирует JSON для вкладки настроек.

## Последовательность работы

1. На старте вызывается `ensureDefaultTemperatureProfiles()`, которое создаёт
   заготовки в `UserTmpProf_1..10`, если они отсутствуют.
2. `TempRegulator::loadTemperatureProfiles()` и `WebInterface::processInitDataToWeb()`
   читают данные через общий `preferences` и отправляют их в UI и браузер.
3. Когда пользователь сохраняет профиль в вебе, `ParseProfileDataFromWeb()`
   разбирает JSON и передаёт данные в `SaveProfileDataToNVS()`, которая напрямую
   обновляет ключи `sNameProfile`, `isAvlablForWeb`, `visible` и таблицу строк в
   выбранном пространстве NVS.
4. После записи веб-интерфейс инициирует `TempRegulator::loadTemperatureProfiles()`,
   чтобы соответствующий экземпляр `TemperatureProfile` перечитал данные и отразил
   изменения в UI и PID-логике.

## Отладка

* Все ключевые операции (`SaveProfileDataToNVS`, `ClearProfileDataFromNVS`,
  ошибки открытия пространства) логируются через `Serial`, что упрощает сбор
  диагностических сообщений.
* Для тестирования можно удалить пространство NVS через инструменты ESP32 или
  вызвать `ClearProfileDataFromNVS()` из веба — профиль будет сброшен к нулевым
  значениям.

