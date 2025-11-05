#include "WebInterface.h"                                                  // Реализация веб-интерфейса

#include <LittleFS.h>                                                      // Отдаём статику из файловой системы
#include <Preferences.h>                                                   // Используем NVS для настроек
#include <ArduinoJson.h>                                                   // JSON
#include <math.h>                                                          // fabsf для сравнения температур
#include <cstring>                                                         // Modified: memset для буфера распарсенных строк

#include "TempRegulator.h"                                                 // Доступ к данным регулятора
#include "TemperatureProfile.h"                                            // Работа с профилями
#include "GlobalPreferences.h"                                             // Modified: общий экземпляр Preferences

namespace {                                                                 // Modified: служебные структуры веб-интерфейса

DynamicJsonDocument g_wsEventDoc(4096);                                     // Modified: общий буфер JSON для сообщений WS
DynamicJsonDocument g_profileParseDoc(4096);                                // Modified: общий буфер JSON для парсинга профилей

struct WebSettings {                                                        // Modified: набор веб-настроек для сохранения
  uint8_t  activProf   = 0;                                                 // Modified: активный профиль
  uint16_t speedHot    = 0;                                                 // Modified: скорость нагрева
  uint16_t tRoom       = 0;                                                 // Modified: температура помещения
  bool     isKalibrate = false;                                             // Modified: признак выполненной калибровки
};

bool saveWebSettings(const char* ns, const WebSettings& s) {                // Modified: сохраняем настройки веба в NVS
  PreferencesLock lock;                                                     // Modified: защищаем доступ к Preferences
  if (!preferences.begin(ns, /*readOnly=*/false)) {                         // Modified: открываем пространство записи
    Serial.println("[WebInterface] Failed to open settings namespace");    // Modified: логируем ошибку
    return false;
  }

  preferences.putUChar("activProf", s.activProf);                          // Modified: активный профиль
  preferences.putBool("isKalibrate", s.isKalibrate);                       // Modified: признак калибровки
  preferences.putUShort("speedHot", s.speedHot);                           // Modified: скорость нагрева
  preferences.putUShort("tRoom", s.tRoom);                                 // Modified: комнатная температура

  preferences.end();                                                        // Modified: закрываем namespace
  return true;
}

}  // namespace

// --------------------------------------------------------------------------------------
// Singleton
// --------------------------------------------------------------------------------------
WebInterface* WebInterface::self_ = nullptr;

WebInterface& WebInterface::instance() {
  static WebInterface inst;                                                // Статический объект
  self_ = &inst;                                                           // Запоминаем адрес
  return inst;                                                             // Возвращаем ссылку
}

// --------------------------------------------------------------------------------------
// CTOR/Init
// --------------------------------------------------------------------------------------
WebInterface::WebInterface() : server_(80), socket_(1337) {}

void WebInterface::begin(TempRegulator* regulator) {
  regulator_ = regulator;
  self_ = this;

  if (!LittleFS.begin()) {
    Serial.println("[WebInterface] Failed to mount LittleFS");
  }

  // HTTP: статика
  server_.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(LittleFS, "/index.html", "text/html");
  });
  server_.on("/style.css", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(LittleFS, "/style.css", "text/css");
  });
  server_.on("/ErrorAlarm.jpg", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(LittleFS, "/ErrorAlarm.jpg", "image/jpeg");
  });
  server_.on("/NeedCalibration.jpg", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(LittleFS, "/NeedCalibration.jpg", "image/jpeg");
  });
  server_.onNotFound([](AsyncWebServerRequest* request) {
    request->send(404, "text/plain", "Not found");
  });

  // WebSocket
  socket_.begin();
  socket_.onEvent(handleWebSocketEvent);

  server_.begin();

  Serial.println("[WebInterface] HTTP & WS started");
}

void WebInterface::loop() {
  socket_.loop();                      // Обслуживаем WebSocket
  broadcastTelemetry();                // Отправляем дифф телеметрии
}

// --------------------------------------------------------------------------------------
// Интеграция с регулятором
// --------------------------------------------------------------------------------------
void WebInterface::updateTelemetry(const TempRegulator& regulator) {
  newActualTempC_ = regulator.getLastTemperatureC();
  newSeltemp_     = String(regulator.getTargetC(), 1);
  newActivprof_   = String(regulator.getActiveProfileIndex());
  newStateprofil_ = regulator.describeStateForWeb();
}

void WebInterface::setProfileAlarm(bool active, const String& message) {
  newProfisAlarm_ = active;
  if (active) {
    newStateprofil_ = message;
  }
}

void WebInterface::setRegulatorAlarm(bool active, const String& message) {
  newRegisAlarm_   = active;
  if (active) {
    errValRegisAlarm_ = message;
  }
}

void WebInterface::noteProfileStart(uint32_t ms) {
  newTimestartprofil_ = String(ms);
  newTimestopprofil_  = "";
}

void WebInterface::noteProfileStop() {
  newTimestopprofil_ = "stop";
}

// --------------------------------------------------------------------------------------
// WebSocket: обработчик событий
// --------------------------------------------------------------------------------------
void WebInterface::handleWebSocketEvent(uint8_t client_num,
                                        WStype_t type,
                                        uint8_t* payload,
                                        size_t length) {
  if (!self_) return;

  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("[WS] Client %u disconnected\n", client_num);
      break;

    case WStype_CONNECTED: {
      IPAddress ip = self_->socket_.remoteIP(client_num);
      Serial.printf("[WS] Client %u connected from %s\n", client_num, ip.toString().c_str());
      break;
    }

    case WStype_TEXT: {
      const String text = String((const char*)payload).substring(0, length);
      Serial.printf("[WS] << %s\n", text.c_str());

      // Простой текстовый командный пакет
      if (text == "InitProfil") {
        self_->processInitDataToWeb();
        break;
      }

      // JSON
      g_wsEventDoc.clear();                                                 // Modified: очищаем общий JSON-буфер перед разбором
      DeserializationError err = deserializeJson(g_wsEventDoc, text);       // Modified: парсим сообщение в глобальный буфер
      if (err) {
        Serial.printf("[WS] JSON parse error: %s\n", err.c_str());
        break;
      }

      const String event = g_wsEventDoc["eventMessage"].as<String>();      // Modified: читаем из общего документа
      if (event == "SaveProfil") {
        // Вместо processSaveRequest: парсим и сохраняем отдельно
        if (self_->ParseProfileDataFromWeb(payload, length)) {              // Modified: сохраняем результат парсинга во внутренних буферах
          self_->SaveProfileDataToNVS(self_->parsedNamespace_,              // Modified: записываем распарсенные данные в NVS
                                      self_->parsedProfileName_,
                                      self_->parsedAvailableForWeb_,
                                      self_->parsedRows_);
          if (self_->regulator_) {                                         // Modified: после записи обновляем профили регулятора
            self_->regulator_->loadTemperatureProfiles();
          }
        }
      } else if (event == "DelProfil") {
        // Вместо processDeleteRequest: обнуляем профиль в NVS
        const String ns = g_wsEventDoc["sNVSnamespace"].as<String>();      // Modified: используем глобальный документ
        if (ns.length() == 0) {
          Serial.println("[WS] DelProfil ignored: empty namespace");
        } else {
          self_->ClearProfileDataFromNVS(ns);
        }
      } else if (event == "EmulSetting") {
        self_->processSettingsRequest(g_wsEventDoc);                        // Modified: передаём общий документ настроек
      }

      self_->processDebugFlags(g_wsEventDoc);                               // Modified: обновляем отладочные флаги из буфера
      break;
    }

    default:
      break;
  }
}

// --------------------------------------------------------------------------------------
// Формирование JSON профиля/настроек из NVS
// --------------------------------------------------------------------------------------
String WebInterface::ExportToJSON(const String& sNVSnamespace) {
  DynamicJsonDocument doc(2048);                                         // Modified: увеличили буфер под полный профиль

  TemperatureProfile profile;                                            // Modified: используем единый класс профиля
  profile.setNamespace(sNVSnamespace);                                   // Modified: выбираем пространство NVS
  bool loaded = profile.loadFromNVS();                                   // Modified: загружаем данные через TemperatureProfile
  if (!loaded) {                                                         // Modified: проверяем успешность доступа к NVS
    Serial.println("Failed to open NVS namespace for reading in ExportToJSON");  // Modified: повторяем прежний лог

    if (profile.saveToNVS(profile.name(), nullptr, 0, false)) {          // Modified: создаём пустой профиль при первом обращении
      loaded = profile.loadFromNVS();                                    // Modified: повторяем чтение после инициализации
    }

    if (!loaded) {                                                       // Modified: если повторная попытка не удалась
      String jsonStr;                                                    // Modified: возвращаем "null" для несовместимых профилей
      serializeJson(doc, jsonStr);                                       // Modified: doc пустой, сериализация даст "null"
      return jsonStr;                                                    // Modified: прекращаем выполнение
    }
  }

  profile.exportToJson(doc);                                             // Modified: формируем JSON тем же методом, что использует контроллер

  String jsonStr;                                                        // Modified: сериализуем готовый объект
  serializeJson(doc, jsonStr);                                           // Modified: получаем строку JSON
  return jsonStr;                                                        // Modified: отдаём её вызывающему коду
}

String WebInterface::EmulSettingsToJSON(const String& sNVSnamespace) {
  DynamicJsonDocument doc(1024);

  PreferencesLock lock;                                                     // Modified: эксклюзивный доступ на время чтения
  if (preferences.begin(sNVSnamespace.c_str(), true)) {
    Serial.println(sNVSnamespace.c_str());
    doc["activProf"]   = preferences.getUInt("activProf", 0);
    doc["isKalibrate"] = preferences.getBool("isKalibrate", false);
    doc["speedHot"]    = preferences.getUInt("speedHot", 1);
    doc["tRoom"]       = preferences.getUInt("tRoom", 25);
    preferences.end();
  } else {
    Serial.println("Failed to open NVS namespace for reading in EmulSettingsToJSON");
  }

  String jsonStr;
  serializeJson(doc, jsonStr);
  return jsonStr;
}

// --------------------------------------------------------------------------------------
// Инициализация фронта данными
// --------------------------------------------------------------------------------------
void WebInterface::processInitDataToWeb() {
  String DictProfSMS = "{\"eventMessage\": \"InitProfil\",";
  for (int i = 1; i <= static_cast<int>(kTemperatureProfileCount); i++) { // Modified: используем общее количество профилей
    const String ns = "UserTmpProf_" + String(i);
    const String js = ExportToJSON(ns);
    Serial.println(ns);
    Serial.println(js);
    if (js.length() > 0) {
      DictProfSMS += "\"" + ns + "\": " + js + ",";
    }
  }
  const String DictEmulSeting = EmulSettingsToJSON("Settings");
  DictProfSMS += "\"Settings\": " + DictEmulSeting + "}";

  Serial.println(DictProfSMS);
  socket_.broadcastTXT(DictProfSMS);
}

// --------------------------------------------------------------------------------------
// Запись профиля в NVS (низкоуровневая)
// --------------------------------------------------------------------------------------
void WebInterface::SaveProfileDataToNVS(const String& sNVSnamespaceKey,
                                        const String& sProfileName,
                                        bool xIsAvailableForWeb,
                                        TempProfileRow dataTempProfileRows[TemperatureProfile::MAX_ROWS]) {
  PreferencesLock lock;                                                   // Modified: синхронизируем доступ к общему Preferences
  if (!preferences.begin(sNVSnamespaceKey.c_str(), false)) {              // Modified: открываем нужное пространство на запись
    Serial.println("Failed to open NVS namespace for reading");          // Modified: повторяем исходный лог ошибки
    return;                                                               // Modified: прекращаем сохранение при ошибке
  }

  preferences.putString("sNameProfile", sProfileName);                   // Modified: записываем имя профиля
  preferences.putString("name",        sProfileName);                    // Modified: совместимость со старым ключом имени
  preferences.putBool("isAvlablForWeb", xIsAvailableForWeb);             // Modified: флаг отображения в вебе
  preferences.putBool("visible",        xIsAvailableForWeb);             // Modified: синхронизируем с LVGL

  for (int i = 0; i < TemperatureProfile::MAX_ROWS; ++i) {                // Modified: сохраняем каждую строку температурного профиля
    String baseKey = "row" + String(i) + "_";                            // Modified: базовый префикс ключа в NVS
    preferences.putFloat((baseKey + "rStartTemp").c_str(),               // Modified: стартовая температура ступени
                         dataTempProfileRows[i].rStartTemperature);
    preferences.putFloat((baseKey + "rEndTemp").c_str(),                 // Modified: конечная температура ступени
                         dataTempProfileRows[i].rEndTemperature);
    preferences.putFloat((baseKey + "rTime").c_str(),                    // Modified: длительность ступени
                         dataTempProfileRows[i].rTime);
  }

  preferences.end();                                                      // Modified: закрываем пространство после записи
  Serial.printf("[WS] Profile '%s' saved to NVS\n",                       // Modified: подтверждаем успешную запись
                sNVSnamespaceKey.c_str());
}

// --------------------------------------------------------------------------------------
// Обнуление профиля в NVS (вместо delete)
// --------------------------------------------------------------------------------------
void WebInterface::ClearProfileDataFromNVS(const String& sNVSnamespaceKey) {
  TemperatureProfile profile;                                            // Modified: используем класс профиля
  profile.setNamespace(sNVSnamespaceKey);                                // Modified: выбираем пространство

  if (profile.clearInNVS()) {                                            // Modified: очищаем через saveToNVS()
    Serial.printf("[WS] NVS cleared for namespace '%s'\n",               // Modified: подтверждаем очистку
                  sNVSnamespaceKey.c_str());

    if (regulator_) {                                                    // Modified: обновляем кэш профилей регулятора
      regulator_->loadTemperatureProfiles();
    }
  } else {
    Serial.println("[WS] Failed to open NVS namespace for clearing");    // Modified: сигнализируем об ошибке
  }
}

// --------------------------------------------------------------------------------------
// Парсинг профиля из входящего JSON-текста и сохранение результата во внутренних буферах
// --------------------------------------------------------------------------------------
bool WebInterface::ParseProfileDataFromWeb(uint8_t* payload, size_t length) {
  memset(parsedRows_, 0, sizeof(parsedRows_));                             // Modified: очищаем буфер строк перед чтением
  const String msg = String((const char*)payload).substring(0, length);
  if (msg.length() == 0) return false;                                     // Modified: пустое сообщение — парсинг не выполнен

  g_profileParseDoc.clear();                                               // Modified: подготавливаем глобальный буфер профиля
  DeserializationError error = deserializeJson(g_profileParseDoc, msg);    // Modified: разбираем JSON профиля
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return false;                                                          // Modified: при ошибке сообщаем о неудаче
  }

  // Парсинг:
  parsedNamespace_ = g_profileParseDoc["sNVSnamespace"].as<String>();    // Modified: сохраняем пространство NVS
  if (parsedNamespace_.isEmpty()) {                                        // Modified: namespace обязателен для записи
    Serial.println("[WS] ParseProfil ignored: empty namespace");          // Modified: выводим причину отказа
    return false;                                                           // Modified: прекращаем обработку
  }
  JsonObject joTempProfileJSON  = g_profileParseDoc[parsedNamespace_.c_str()]; // Modified: объект профиля
  if (joTempProfileJSON.isNull()) {                                         // Modified: проверяем наличие данных профиля
    Serial.println("[WS] ParseProfil ignored: profile object missing");    // Modified: логируем отсутствие раздела
    return false;                                                           // Modified: прекращаем обработку
  }
  parsedProfileName_ = joTempProfileJSON["sNameProfile"].as<String>();   // Modified: имя профиля
  parsedAvailableForWeb_ = joTempProfileJSON["isAvailableForWeb"].as<bool>(); // Modified: доступность в вебе
  JsonArray jaTempProfileDataTable = joTempProfileJSON["data"];
  if (jaTempProfileDataTable.isNull()) {                                   // Modified: проверяем наличие таблицы данных
    Serial.println("[WS] ParseProfil ignored: data array missing");       // Modified: логируем ошибку структуры
    return false;                                                           // Modified: прекращаем парсинг
  }

  for (size_t i = 0; i < jaTempProfileDataTable.size() && i < TemperatureProfile::MAX_ROWS; i++) {
    JsonObject row = jaTempProfileDataTable[i];

    char key1[6], key2[6], key3[6];
    snprintf(key1, sizeof(key1), "%u_1", i + 1);
    snprintf(key2, sizeof(key2), "%u_2", i + 1);
    snprintf(key3, sizeof(key3), "%u_3", i + 1);

    parsedRows_[i].rStartTemperature = row[key1].as<float>();              // Modified: заполняем буфер строк
    parsedRows_[i].rEndTemperature   = row[key2].as<float>();              // Modified: конечная температура
    parsedRows_[i].rTime             = row[key3].as<float>();              // Modified: длительность ступени
  }

  // DEBUG
  for (int i = 0; i < TemperatureProfile::MAX_ROWS; i++) {
    Serial.print("Row "); Serial.print(i + 1); Serial.print(": ");
    Serial.print(parsedRows_[i].rStartTemperature); Serial.print("  ");
    Serial.print(parsedRows_[i].rEndTemperature);   Serial.print("  ");
    Serial.println(parsedRows_[i].rTime);
  }

  return true;                                                             // Modified: сигнализируем вызывающему коду
}

// --------------------------------------------------------------------------------------
// Настройки/флаги
// --------------------------------------------------------------------------------------
void WebInterface::processSettingsRequest(const JsonDocument& doc) {
  String ns = doc["sNVSnamespace"].as<String>();
  if (ns.isEmpty()) ns = "Settings";

  JsonObjectConst settingsObj = doc["EmulSettings"].as<JsonObjectConst>();
  if (settingsObj.isNull()) {
    Serial.println("[WS] EmulSetting ignored: no settings object");
    return;
  }

  WebSettings s{};
  s.activProf   = settingsObj["activProf"].as<uint8_t>();
  s.speedHot    = settingsObj["speedHot"].as<uint16_t>();
  s.tRoom       = settingsObj["tRoom"].as<uint16_t>();
  s.isKalibrate = settingsObj["isKalibrate"].as<bool>();

  saveWebSettings(ns.c_str(), s);

  newActivprof_ = String(s.activProf);   // Чтобы фронт сразу увидел актуальный профиль
}

void WebInterface::processDebugFlags(const JsonDocument& doc) {
  if (doc.containsKey("profisAlarm"))         newProfisAlarm_        = doc["profisAlarm"].as<bool>();
  if (doc.containsKey("regisAlarm"))          newRegisAlarm_         = doc["regisAlarm"].as<bool>();
  if (doc.containsKey("emulTimestartprofil")) newTimestartprofil_    = doc["emulTimestartprofil"].as<String>();
  if (doc.containsKey("emulTimestopprofil"))  newTimestopprofil_     = doc["emulTimestopprofil"].as<String>();
  if (doc.containsKey("emulTimestartstupen")) newTimestartstupen_    = doc["emulTimestartstupen"].as<String>();
  if (doc.containsKey("emulTimestopstupen"))  newTimestopstupen_     = doc["emulTimestopstupen"].as<String>();
  if (doc.containsKey("emulNstupen"))         newNstupen_            = doc["emulNstupen"].as<String>();
  if (doc.containsKey("emulActivprof"))       newActivprof_          = doc["emulActivprof"].as<String>();
  if (doc.containsKey("emulSeltemp"))         newSeltemp_            = doc["emulSeltemp"].as<String>();
}

// --------------------------------------------------------------------------------------
// Дифф-телеметрия
// --------------------------------------------------------------------------------------
void WebInterface::broadcastTelemetry() {
  String msg = buildDiffMessage();                                        // Modified: создаём изменяемую строку для WS
  if (msg.isEmpty()) return;

  socket_.broadcastTXT(msg);
  Serial.printf("[WS] >> %s\n", msg.c_str());
}

String WebInterface::buildDiffMessage() {
  DynamicJsonDocument diff(1024);
  bool changed = false;

  if (profisAlarm_ != newProfisAlarm_) {
    diff["profisAlarm"] = newProfisAlarm_;
    profisAlarm_ = newProfisAlarm_;
    changed = true;
  }

  if (regisAlarm_ != newRegisAlarm_) {
    diff["regisAlarm"]       = newRegisAlarm_;
    diff["ErrValregisAlarm"] = errValRegisAlarm_;
    regisAlarm_ = newRegisAlarm_;
    changed = true;
  }

  if (timestartprofil_ != newTimestartprofil_) {
    diff["timestartprofil"] = newTimestartprofil_;
    timestartprofil_ = newTimestartprofil_;
    changed = true;
  }

  if (timestopprofil_ != newTimestopprofil_) {
    diff["timestopprofil"] = newTimestopprofil_;
    timestopprofil_ = newTimestopprofil_;
    changed = true;
  }

  if (timestartstupen_ != newTimestartstupen_) {
    diff["timestartstupen"] = newTimestartstupen_;
    timestartstupen_ = newTimestartstupen_;
    changed = true;
  }

  if (timestopstupen_ != newTimestopstupen_) {
    diff["timestopstupen"] = newTimestopstupen_;
    timestopstupen_ = newTimestopstupen_;
    changed = true;
  }

  if (nstupen_ != newNstupen_) {
    diff["nstupen"] = newNstupen_;
    nstupen_ = newNstupen_;
    changed = true;
  }

  if (activprof_ != newActivprof_) {
    diff["activprof"] = newActivprof_.length() ? newActivprof_.toInt() : 0;
    activprof_ = newActivprof_;
    changed = true;
  }

  if (seltemp_ != newSeltemp_) {
    diff["seltemp"] = newSeltemp_.length() ? newSeltemp_.toFloat() : 0.0f;
    seltemp_ = newSeltemp_;
    changed = true;
  }

  if (stateprofil_ != newStateprofil_) {
    diff["stateprofil"] = newStateprofil_;
    stateprofil_ = newStateprofil_;
    changed = true;
  }

  if (fabsf(actualTempC_ - newActualTempC_) > 0.01f) {
    diff["actualtemp"] = newActualTempC_;
    actualTempC_ = newActualTempC_;
    changed = true;
  }

  if (!changed) return String();

  String out;
  serializeJson(diff, out);
  return out;
}
