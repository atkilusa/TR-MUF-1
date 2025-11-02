#include "TemperatureProfile.h"                                           // подключаем объявление класса профиля
#include <Preferences.h>                                                  // используем NVS для сохранения профилей

namespace {                                                               // внутренние вспомогательные структуры

struct DefaultProfileDefinition {                                          // описание профиля по умолчанию
  const char* nspace;                                                      // имя пространства NVS
  const char* displayName;                                                 // отображаемое имя
  bool showOnWeb;                                                          // флаг видимости в веб-интерфейсе
};

constexpr DefaultProfileDefinition kDefaultProfiles[] = {                  // таблица с 10 заготовками
    {"UserTmpProf_1",  "\xD0\xA2\xD0\xB5\xD1\x81\xD1\x82\xD0\xBE\xD0\xB2\xD1\x8B\xD0\xB9 \xD0\xBF\xD1\x80\xD0\xBE\xD1\x84\xD0\xB8\xD0\xBB", true},
    {"UserTmpProf_2",  "", false},
    {"UserTmpProf_3",  "", false},
    {"UserTmpProf_4",  "", false},
    {"UserTmpProf_5",  "", false},
    {"UserTmpProf_6",  "", false},
    {"UserTmpProf_7",  "", false},
    {"UserTmpProf_8",  "", false},
    {"UserTmpProf_9",  "", false},
    {"UserTmpProf_10", "", false},
};

constexpr size_t kDefaultProfileCount =
    sizeof(kDefaultProfiles) / sizeof(kDefaultProfiles[0]);

void resetRowsInPrefs(Preferences& prefs) {                               // заполняем таблицу нулями
  for (int i = 0; i < TemperatureProfile::MAX_ROWS; ++i) {
    String baseKey = "row" + String(i) + "_";
    prefs.putFloat((baseKey + "rStartTemp").c_str(), 0.0f);
    prefs.putFloat((baseKey + "rEndTemp").c_str(),   0.0f);
    prefs.putFloat((baseKey + "rTime").c_str(),      0.0f);
  }
}

void writeProfileDefaults(const DefaultProfileDefinition& def) {          // записывает дефолтные значения
  Preferences prefs;
  if (!prefs.begin(def.nspace, false)) {
    return;
  }

  bool hasAny = prefs.isKey("sNameProfile") || prefs.isKey("name") || prefs.isKey("isAvlablForWeb");
  if (!hasAny) {
    prefs.putString("sNameProfile", def.displayName);
    prefs.putString("name",        def.displayName);
    prefs.putBool("isAvlablForWeb", def.showOnWeb);
    prefs.putBool("visible",        def.showOnWeb);
    prefs.putDouble("rKp_PWM", 0.0);
    prefs.putDouble("rKi_PWM", 0.0);
    prefs.putDouble("rKd_PWM", 0.0);
    prefs.putDouble("rKl_TC", 1.0);
    prefs.putDouble("rKc_TC", 0.0);
    resetRowsInPrefs(prefs);
  }

  prefs.end();
}

}  // namespace

// --------------------------------------------------------------------------------------
// TemperatureProfile
// --------------------------------------------------------------------------------------
TemperatureProfile::TemperatureProfile() {
  resetRows();
}

TemperatureProfile::TemperatureProfile(const String& ns, const String& defaultName)
    : sNVSnamespace(ns), sNameProfile(defaultName) {
  resetRows();
}

void TemperatureProfile::setNamespace(const String& ns) {
  sNVSnamespace = ns;
}

void TemperatureProfile::setDefaultName(const String& name) {
  sNameProfile = name;
}

bool TemperatureProfile::loadFromNVS() {
  Preferences prefs;

  if (sNVSnamespace.isEmpty()) {
    return false;
  }

  // ВАЖНО: перед загрузкой очищаем актуальное состояние,
  // чтобы не накапливались старые значения при повторных вызовах
  available       = false;
  availableForWeb = false;
  showInMenu      = false;
  usedRows        = 0;
  // НЕ сбрасываем PID в ноль без нужды — читаем их с дефолтами из текущих значений:
  // если ключей нет, сохранятся прежние поля объекта.
  resetRows();

  if (!prefs.begin(sNVSnamespace.c_str(), true)) {
    return false;
  }

  String storedName = prefs.getString("sNameProfile",
                         prefs.getString("name", sNameProfile));
  storedName.trim();
  if (storedName.length() > 0) {
    sNameProfile = storedName;
  }

  rKp_PWM = prefs.getDouble("rKp_PWM", rKp_PWM);
  rKi_PWM = prefs.getDouble("rKi_PWM", rKi_PWM);
  rKd_PWM = prefs.getDouble("rKd_PWM", rKd_PWM);
  rKl_TC  = prefs.getDouble("rKl_TC",  rKl_TC);
  rKc_TC  = prefs.getDouble("rKc_TC",  rKc_TC);

  showInMenu      = prefs.getBool("visible", false);
  availableForWeb = prefs.getBool("isAvlablForWeb", showInMenu);

  for (int i = 0; i < MAX_ROWS; ++i) {
    String baseKey = "row" + String(i) + "_";
    rows[i].rStartTemperature = prefs.getFloat((baseKey + "rStartTemp").c_str(), 0.0f);
    rows[i].rEndTemperature   = prefs.getFloat((baseKey + "rEndTemp").c_str(),   0.0f);
    rows[i].rTime             = prefs.getFloat((baseKey + "rTime").c_str(),      0.0f);

    if (rows[i].rTime > 0.0f || rows[i].rStartTemperature != 0.0f || rows[i].rEndTemperature != 0.0f) {
      ++usedRows;
    }
  }

  prefs.end();

  const bool hasSteps = (usedRows > 0);
  if (hasSteps && !showInMenu) {
    showInMenu = true;
  }

  available = showInMenu && hasSteps && sNameProfile.length() > 0;
  return available;
}

// Дополнительный «удобный» метод — явная перезагрузка профиля из NVS
bool TemperatureProfile::UpdateFromNVS() {
  return loadFromNVS();
}

bool TemperatureProfile::saveToNVS(const String& name,
                                   const TempProfileRow* newRows,
                                   size_t rowCount,
                                   bool visibleForWeb) {
  if (sNVSnamespace.isEmpty()) {
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(sNVSnamespace.c_str(), false)) {
    return false;
  }

  prefs.putString("sNameProfile", name);
  prefs.putString("name",         name);
  prefs.putBool("isAvlablForWeb", visibleForWeb);
  prefs.putBool("visible",        visibleForWeb);

  for (int i = 0; i < MAX_ROWS; ++i) {
    TempProfileRow row{};
    if (newRows && static_cast<size_t>(i) < rowCount) {
      row = newRows[i];
    }
    String baseKey = "row" + String(i) + "_";
    prefs.putFloat((baseKey + "rStartTemp").c_str(), row.rStartTemperature);
    prefs.putFloat((baseKey + "rEndTemp").c_str(),   row.rEndTemperature);
    prefs.putFloat((baseKey + "rTime").c_str(),      row.rTime);
  }

  prefs.end();

  // Локально обновим зеркальное состояние объекта: читаем обратно из NVS,
  // чтобы инстанс сразу отражал сохранённые значения (важно для веб-правок).
  return loadFromNVS();
}

bool TemperatureProfile::clearInNVS() {
  TempProfileRow zeroRows[MAX_ROWS]{};
  // Сохраняем «пустой» профиль и одновременно локально обновляемся через loadFromNVS()
  return saveToNVS("", zeroRows, 0, false);
}

bool TemperatureProfile::exportToJson(JsonDocument& doc) const {
  JsonObject obj = doc.to<JsonObject>();
  obj.clear();
  obj["sNVSnamespace"]   = sNVSnamespace;
  obj["sNameProfile"]    = sNameProfile;
  obj["isAvailableForWeb"] = availableForWeb;
  obj["rKp_PWM"] = rKp_PWM;
  obj["rKi_PWM"] = rKi_PWM;
  obj["rKd_PWM"] = rKd_PWM;
  obj["rKl_TC"]  = rKl_TC;
  obj["rKc_TC"]  = rKc_TC;

  JsonArray dataArr = obj.createNestedArray("data");
  for (int i = 0; i < MAX_ROWS; ++i) {
    JsonObject row = dataArr.createNestedObject();
    char key1[6], key2[6], key3[6];
    snprintf(key1, sizeof(key1), "%d_1", i + 1);
    snprintf(key2, sizeof(key2), "%d_2", i + 1);
    snprintf(key3, sizeof(key3), "%d_3", i + 1);
    row[key1] = rows[i].rStartTemperature;
    row[key2] = rows[i].rEndTemperature;
    row[key3] = rows[i].rTime;
  }

  return true;
}

bool TemperatureProfile::hasPidCoefficients() const {
  return (rKp_PWM != 0.0) || (rKi_PWM != 0.0) || (rKd_PWM != 0.0);
}

const TempProfileRow& TemperatureProfile::step(size_t idx) const {
  static TempProfileRow empty{};
  if (idx < MAX_ROWS) {
    return rows[idx];
  }
  return empty;
}

void TemperatureProfile::resetRows() {
  for (int i = 0; i < MAX_ROWS; ++i) {
    rows[i] = TempProfileRow{};
  }
}

void ensureDefaultTemperatureProfiles() {
  for (size_t i = 0; i < kDefaultProfileCount; ++i) {
    writeProfileDefaults(kDefaultProfiles[i]);
  }
}

