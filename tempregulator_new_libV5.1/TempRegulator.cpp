#include "TempRegulator.h"

#include <Arduino.h>
#include <algorithm>
#include <initializer_list>
#include <limits>
#include <string>
#include <cmath>

#include "DisplayDriver.h"
#include "EncoderInput.h"
#include "TouchCalibration.h"
#include "HardwareConfig.h"
#include "Storage.h"
#include "LogoImageBuiltin.h"
#include "WebInterface.h"                                                // Modified: обновляем WebSocket при изменениях

#include <LittleFS.h>
#include "esp_timer.h"
#include "esp_system.h"

#if LVGL_VERSION_MAJOR >= 9
#define LV_STATE_FOCUS_COMPAT LV_STATE_FOCUSED
#define LV_STATE_EDIT_COMPAT  LV_STATE_EDITED
#else
#define LV_STATE_FOCUS_COMPAT LV_STATE_FOCUSED
#define LV_STATE_EDIT_COMPAT  LV_STATE_EDITED
#endif

TempRegulator regulator;

/* ====== STYLE: иконки Montserrat16 отдельным лейблом ====== */
static lv_style_t g_icon_m16_style;
static bool g_icon_m16_inited = false;

static inline void ensure_icon_m16_style() {
  if (!g_icon_m16_inited) {
    lv_style_init(&g_icon_m16_style);
    lv_style_set_text_font(&g_icon_m16_style, &lv_font_montserrat_16);
    g_icon_m16_inited = true;
  }
}

/* стиль рамки для выбранных элементов списка/кнопок */
static lv_style_t g_focus_outline_style;
static lv_style_t g_focus_outline_accent_style;
static bool g_focus_outline_inited = false;
static bool g_focus_outline_accent_inited = false;

static inline void ensure_focus_outline_style(lv_style_t* style,
                                              bool* inited,
                                              lv_color_t color) {
  if (*inited) {
    return;
  }
  lv_style_init(style);
  lv_style_set_outline_width(style, 0);
  lv_style_set_outline_opa(style, LV_OPA_TRANSP);
  lv_style_set_bg_color(style, color);
  lv_style_set_bg_opa(style, LV_OPA_COVER);
  lv_style_set_radius(style, 8);
  *inited = true;
}

static inline const lv_style_t* get_focus_style(bool accent) {
  if (accent) {
    ensure_focus_outline_style(&g_focus_outline_accent_style,
                               &g_focus_outline_accent_inited,
                               lv_palette_lighten(LV_PALETTE_GREEN, 2));
    return &g_focus_outline_accent_style;
  }
  ensure_focus_outline_style(&g_focus_outline_style,
                             &g_focus_outline_inited,
                             lv_palette_main(LV_PALETTE_BLUE));
  return &g_focus_outline_style;
}

static inline void apply_focus_style(lv_obj_t* obj, bool accent = false) {
  const lv_style_t* style = get_focus_style(accent);
  lv_obj_add_style(obj, style, LV_PART_MAIN | LV_STATE_FOCUS_COMPAT);
  lv_obj_add_style(obj,
                   style,
                   LV_PART_MAIN | LV_STATE_FOCUS_COMPAT | LV_STATE_EDIT_COMPAT);
}

/* list button + icon (слева) */
static inline lv_obj_t* list_add_btn_with_icon(lv_obj_t* list,
                                               const char* sym,
                                               const char* text,
                                               bool accent = false) {
  lv_obj_t* btn = lv_list_add_button(list, NULL, text);
  lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_gap(btn, 8, 0);
  apply_focus_style(btn, accent);
  lv_obj_t* txt = lv_obj_get_child(btn, 0);
  ensure_icon_m16_style();
  lv_obj_t* icon = lv_label_create(btn);
  lv_obj_add_style(icon, &g_icon_m16_style, 0);
  lv_label_set_text(icon, sym);
  lv_obj_move_to_index(icon, 0);
  if (txt) lv_obj_move_to_index(txt, 1);
  return btn;
}

/* button с иконкой + текстом */
static inline lv_obj_t* make_btn_with_icon(lv_obj_t* parent,
                                          const char* sym,
                                          const char* text,
                                          bool accent = false) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_gap(b, 8, 0);
  apply_focus_style(b, accent);
  ensure_icon_m16_style();
  lv_obj_t* icon = lv_label_create(b);
  lv_obj_add_style(icon, &g_icon_m16_style, 0);
  lv_label_set_text(icon, sym);
  lv_obj_t* lbl = lv_label_create(b);
  lv_label_set_text(lbl, text);
  return b;
}

/* только иконка */
static inline lv_obj_t* make_icon_only_btn(lv_obj_t* parent,
                                           const char* sym,
                                           bool accent = false) {
  lv_obj_t* b = lv_btn_create(parent);
  apply_focus_style(b, accent);
  ensure_icon_m16_style();
  lv_obj_t* icon = lv_label_create(b);
  lv_obj_add_style(icon, &g_icon_m16_style, 0);
  lv_label_set_text(icon, sym);
  return b;
}

/* плавная загрузка экрана */
static inline void scr_load_smooth(lv_obj_t* scr) {
  lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 180, 0, true);
}
static lv_group_t* ui_group = nullptr;
static lv_group_t* g_encoder_modal_group = nullptr;
static lv_group_t* g_encoder_prev_group  = nullptr;
static void encoder_modal_release();

static lv_indev_t* encoder_indev() {
  return EncoderInput::getInputDevice();
}

static void set_encoder_group(lv_group_t* group) {
  if (auto indev = encoder_indev()) {
    lv_indev_set_group(indev, group);
  }
}

static void clear_encoder_group() {
  encoder_modal_release();
  if (!ui_group) {
    return;
  }
  if (auto indev = encoder_indev()) {
    lv_indev_set_group(indev, nullptr);
  }
  lv_group_del(ui_group);
  ui_group = nullptr;
}

static void encoder_modal_take(std::initializer_list<lv_obj_t*> buttons) {
  if (g_encoder_modal_group) {
    lv_group_del(g_encoder_modal_group);
    g_encoder_modal_group = nullptr;
  }

  g_encoder_prev_group = nullptr;
  if (auto indev = encoder_indev()) {
    g_encoder_prev_group = lv_indev_get_group(indev);
  }

  g_encoder_modal_group = lv_group_create();
  for (lv_obj_t* btn : buttons) {
    if (btn) {
      lv_group_add_obj(g_encoder_modal_group, btn);
    }
  }

  if (auto indev = encoder_indev()) {
    lv_indev_set_group(indev, g_encoder_modal_group);
  }
}

static void encoder_modal_release() {
  if (auto indev = encoder_indev()) {
    lv_indev_set_group(indev, g_encoder_prev_group);
  }
  if (g_encoder_modal_group) {
    lv_group_del(g_encoder_modal_group);
    g_encoder_modal_group = nullptr;
  }
  g_encoder_prev_group = nullptr;
}

/* LVGL tick 5 ms */
static void lv_tick_task(void* arg){ lv_tick_inc(5); }

/* ========= Consts ========= */
static constexpr uint8_t  ADC_READ_SAMPLES        = 21;
static constexpr uint16_t ADC_OUTLIER_THRESHOLD   = 50;
static constexpr uint8_t  ADC_OUTLIER_ALARM_COUNT = 5;
static constexpr uint32_t SSR_WINDOW_MS           = 1000;

static constexpr uint16_t CAL_MAX_OUTLIERS   = 5;
static constexpr uint16_t CAL_MIN_ADC_DIFF   = 120;
static constexpr uint16_t CAL_STABLE_DELTA   = 8;
static constexpr uint32_t CAL_STABLE_HOLD_MS = 2000;
static constexpr uint32_t CAL_STEP_TIMEOUT_MS= 60000;

static constexpr float    AT_MIN_TARGET_C = 40.0f;
static constexpr float    AT_MAX_TARGET_C = 500.0f;
static constexpr uint32_t AT_TIMEOUT_MS   = 10*60*1000;

/* Header UI */
static constexpr int HEADER_H = 28;
static inline void place_below_header(lv_obj_t* obj, int ypad = 6) {
  lv_obj_align(obj, LV_ALIGN_TOP_MID, 0, HEADER_H + ypad);
}

static void _close_mbox_only_cb(lv_event_t* ev);

lv_obj_t* TempRegulator::msgbox(const char* text, const char* btn1) {
  lv_obj_t* m = lv_msgbox_create(NULL);
  lv_msgbox_add_text(m, text);
  lv_obj_center(m);
  if (btn1 && *btn1) {
    lv_obj_t* b = lv_msgbox_add_footer_button(m, btn1);
    lv_obj_add_event_cb(b, _close_mbox_only_cb, LV_EVENT_CLICKED, nullptr);
    encoder_modal_take({b});
  } else {
    lv_obj_t* close = lv_msgbox_add_close_button(m);
    lv_obj_add_event_cb(close, _close_mbox_only_cb, LV_EVENT_CLICKED, nullptr);
    encoder_modal_take({close});
  }
  return m;
}

void TempRegulator::clearAlarm() {
  alarm_active = false;
  consecutive_outlier_cycles = 0;
}

/* forward decl. */
extern const lv_point_t kCrossPts[4];
static lv_obj_t* make_cross(lv_obj_t* parent, int x, int y);

/* ====== ПРОТОТИПЫ ГЛОБАЛЬНЫХ КОЛБЭКОВ ====== */
static void _open_info_cb(lv_event_t* ev);
static void _main_profiles_cb(lv_event_t* ev);
static void _main_settings_cb(lv_event_t* ev);
static void _main_manual_cb(lv_event_t* ev);
static void _alarm_ok_cb(lv_event_t* ev);
static void _warn_alarm_cb(lv_event_t* ev);
static void _warn_nocalib_cb(lv_event_t* ev);
static void _triangle_clicked_cb(lv_event_t* ev);
static void _splash_done_cb(lv_timer_t* t);

static void _profiles_item_cb(lv_event_t* ev);
static void _profiles_back_cb(lv_event_t* ev);

static void _settings_calib_cb(lv_event_t* ev);
static void _settings_autotune_cb(lv_event_t* ev);
static void _settings_need_cal_ok_cb(lv_event_t* ev);
static void _settings_advanced_cb(lv_event_t* ev);
static void _settings_reset_cb(lv_event_t* ev);
static void _settings_back_cb(lv_event_t* ev);

static void _advanced_pid_cb(lv_event_t* ev);
static void _advanced_tc_cb(lv_event_t* ev);
static void _advanced_back_cb(lv_event_t* ev);

static void _pid_back_cb(lv_event_t* ev);
static void _pid_reset_open_cb(lv_event_t* ev);
static void _pid_reset_confirm_cb(lv_event_t* ev);
static void _pid_kp_plus_cb(lv_event_t* ev);
static void _pid_kp_minus_cb(lv_event_t* ev);
static void _pid_ki_plus_cb(lv_event_t* ev);
static void _pid_ki_minus_cb(lv_event_t* ev);
static void _pid_kd_plus_cb(lv_event_t* ev);
static void _pid_kd_minus_cb(lv_event_t* ev);

static void _tc_back_cb(lv_event_t* ev);
static void _tc_reset_open_cb(lv_event_t* ev);
static void _tc_reset_confirm_cb(lv_event_t* ev);
static void _tc_kl_plus_cb(lv_event_t* ev);
static void _tc_kl_minus_cb(lv_event_t* ev);
static void _tc_kc_plus_cb(lv_event_t* ev);
static void _tc_kc_minus_cb(lv_event_t* ev);

static void _reset_touch_open_confirm_cb(lv_event_t* ev);
static void _reset_tc_open_confirm_cb(lv_event_t* ev);
static void _reset_pid_open_confirm_cb(lv_event_t* ev);
static void _reset_back_cb(lv_event_t* ev);
static void _ok_reset_touch_cb(lv_event_t* ev);
static void _ok_reset_tc_cb(lv_event_t* ev);
static void _ok_reset_pid_cb(lv_event_t* ev);
static void _open_confirm(TempRegulator* s, const char* txt, lv_event_cb_t ok_cb);
static void _open_exit_confirm(TempRegulator* s);
static void _exit_confirm_yes_cb(lv_event_t* ev);
static void _exit_confirm_no_cb(lv_event_t* ev);

static void _cal1_plus_cb(lv_event_t* ev);
static void _cal1_minus_cb(lv_event_t* ev);
static void _cal1_back_cb(lv_event_t* ev);
static void _cal1_ok_cb(lv_event_t* ev);
static void _cal2_back_cb(lv_event_t* ev);
static void _cal2_ok_cb(lv_event_t* ev);

/* Универсальный тоггл «Стоп/Пуск» */
static void _heat_toggle_cb(lv_event_t* ev);

static void _touchtest_btn_cb(lv_event_t* ev);
static void _touchtest_bg_cb(lv_event_t* ev);

/* Переход назад из профиля */
static void _work_back_cb(lv_event_t* ev);

/* Автонастройка — без лямбд */
static void _at_setup_next_cb(lv_event_t* ev);
static void _at_confirm_no_cb(lv_event_t* ev);
static void _at_confirm_yes_cb(lv_event_t* ev);
static void _at_abort_cb(lv_event_t* ev);

/* Ручной режим — колбэки */
static void _manual_plus_cb(lv_event_t* ev);
static void _manual_minus_cb(lv_event_t* ev);
static void _manual_back_cb(lv_event_t* ev);

/* trampolines for async */
static void _async_enter_settings(void* u);
static void _async_create_calib_s2(void* u);
static void _async_do_reset_touch(void* u);
static void _async_do_reset_tc(void* u);
static void _async_do_reset_pid(void* u);
static void _async_open_profiles(void* u);

static const char* const kProfileNamespaces[kTemperatureProfileCount] = {  // Modified: пространства профилей для веба
  "UserTmpProf_1", "UserTmpProf_2", "UserTmpProf_3", "UserTmpProf_4", "UserTmpProf_5",
  "UserTmpProf_6", "UserTmpProf_7", "UserTmpProf_8", "UserTmpProf_9", "UserTmpProf_10"
};

static const char* const kProfileDefaultNames[kTemperatureProfileCount] = {
  "Профиль 1", "Профиль 2", "Профиль 3", "Профиль 4", "Профиль 5",
  "Профиль 6", "Профиль 7", "Профиль 8", "Профиль 9", "Профиль 10"
};

/* ========= App ========= */

/* ---- Manual setpoint API (реализация) ---- */
float TempRegulator::getTargetC() const { return targetC; }
void  TempRegulator::setTargetC(float c) {
  if (c < 40.0f)  c = 40.0f;
  if (c > 500.0f) c = 500.0f;
  targetC = c;
  pid.setSetpoint(targetC);
}
void  TempRegulator::adjustTargetC(float delta) { setTargetC(targetC + delta); }

void TempRegulator::beep(uint16_t ms) {
  digitalWrite(BUZZER_PIN, LOW);   // Активный уровень — низкий (источник подключен к «плюсу»)
  delay(ms);
  digitalWrite(BUZZER_PIN, HIGH);  // Возвращаемся в неактивное состояние
}

bool TempRegulator::isCalibTimedOut() const {
  return (millis() - cal_t0) > CAL_STEP_TIMEOUT_MS;
}

/* Header maker */
static lv_obj_t* make_header(lv_obj_t* parent, const char* t) {
  lv_obj_set_style_pad_all(parent, 0, 0);
  lv_obj_set_scroll_dir(parent, LV_DIR_NONE);
  lv_obj_t* cont = lv_obj_create(parent);
  lv_obj_remove_style_all(cont);
  lv_obj_set_size(cont, LV_PCT(100), HEADER_H);
  lv_obj_set_pos(cont, 0, 0);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(cont, lv_color_hex(0x2979FF), 0);
  lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_radius(cont, 8, 0);
  lv_obj_t* l = lv_label_create(cont);
  lv_label_set_text(l, t);
  lv_obj_set_style_text_color(l, lv_color_white(), 0);
  lv_obj_center(l);
  return cont;
}

/* Ряд "как в главном меню": слева key, справа value и узкая единица */
static lv_obj_t* make_kv_row(lv_obj_t* parent,
                             const char* key,
                             const char* val,
                             const char* unit,
                             lv_obj_t** out_val_lbl /* optional */)
{
  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, 300, 36);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_left(row, 2, 0);
  lv_obj_set_style_pad_right(row, 2, 0);
  lv_obj_set_style_pad_top(row, 0, 0);
  lv_obj_set_style_pad_bottom(row, 0, 0);
  lv_obj_set_style_pad_gap(row, 2, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* l_key = lv_label_create(row);
  lv_label_set_text(l_key, key);
  lv_obj_set_flex_grow(l_key, 1);
  lv_obj_set_style_text_align(l_key, LV_TEXT_ALIGN_LEFT, 0);

  lv_obj_t* l_val = lv_label_create(row);
  lv_label_set_text(l_val, val ? val : "----");
  lv_obj_set_width(l_val, 116);
  lv_obj_set_style_text_align(l_val, LV_TEXT_ALIGN_RIGHT, 0);

  lv_obj_t* l_unit = lv_label_create(row);
  lv_label_set_text(l_unit, unit ? unit : "");
  lv_obj_set_width(l_unit, 28);
  lv_obj_set_style_text_align(l_unit, LV_TEXT_ALIGN_LEFT, 0);

  if (out_val_lbl) *out_val_lbl = l_val;
  return row;
}

/* ===== Глобальные C-style колбэки ===== */
static void _close_mbox_only_cb(lv_event_t* ev) {
  encoder_modal_release();
  lv_obj_t* btn    = lv_event_get_current_target_obj(ev);
  lv_obj_t* footer = lv_obj_get_parent(btn);
  lv_obj_t* m      = lv_obj_get_parent(footer);
  lv_msgbox_close(m);
}

/* trampolines */
static void _async_enter_settings(void* u){ ((TempRegulator*)u)->onEnterSettings(); }
static void _async_create_calib_s2(void* u){ ((TempRegulator*)u)->createCalibS2(); }
static void _async_do_reset_touch(void* u){ ((TempRegulator*)u)->do_reset_touch(); }
static void _async_do_reset_tc(void* u)   { ((TempRegulator*)u)->do_reset_tc(); }
static void _async_do_reset_pid(void* u)  { ((TempRegulator*)u)->do_reset_pid(); }
static void _async_open_profiles(void* u){((TempRegulator*)u)->createProfiles();}
/* ===== Инфо (глаз) ===== */
void TempRegulator::openInfoDialog() {
  char buf[192];
  snprintf(buf, sizeof(buf),
           "PID:\n  Kp = %.1f\n  Ki = %.1f\n  Kd = %.1f\n\n"
           "Термопара:\n  kl = %.1f\n  kc = %.1f\n \n Калибровка: %s",
           pid_kp, pid_ki, pid_kd,
           (double)slope, (double)offset,
           isCalibrated ? "OK" : "нет");

  lv_obj_t* m = lv_msgbox_create(NULL);
  lv_msgbox_add_text(m, buf);
  lv_obj_center(m);
  lv_obj_t* ok = lv_msgbox_add_footer_button(m, "ОК");
  lv_obj_add_event_cb(ok, _close_mbox_only_cb, LV_EVENT_CLICKED, nullptr);
  encoder_modal_take({ok});
}
static void _open_info_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  s->openInfoDialog();
}

// ==== Splash screen timer callback ====
static void _splash_done_cb(lv_timer_t* t) {
    auto* s = (TempRegulator*) lv_timer_get_user_data(t);
    lv_timer_del(t);
    if (!g_touch_calibrated) s->onEnterTouchCalib();
    else                     s->onEnterTouchTest();
}

static void _triangle_clicked_cb(lv_event_t* ev) {
    auto* s = (TempRegulator*) lv_event_get_user_data(ev);
    if (s->hasAlarm()) {
        s->msgbox("Обнаружена ошибка датчика/системы.\nОткройте «Настройки» для диагностики.");
    } else {
        s->msgbox("Калибровка не выполнена.\nПерейдите: Настройки → Калибровка темопары.");
    }
}

/* ==== Моргание через анимацию (без таймера) ==== */
static void _anim_set_opa(void* obj, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t*)obj, (lv_opa_t)v, 0);
}

// при нажатии на красный треугольник
static void _warn_alarm_cb(lv_event_t* ev) {
    auto s = (TempRegulator*)lv_event_get_user_data(ev);
    s->msgbox("Ошибка датчика!\nСистема в аварии.");
}

// при нажатии на синий треугольник
static void _warn_nocalib_cb(lv_event_t* ev) {
    auto s = (TempRegulator*)lv_event_get_user_data(ev);
    s->msgbox("Нет калибровки!\nВыполните настройку.");
}

// значок предупреждения
static lv_obj_t* make_warning_triangle(lv_obj_t* parent,
                                       lv_color_t color,
                                       lv_event_cb_t cb,
                                       void* user) {
  lv_obj_t* cont = lv_obj_create(parent);
  lv_obj_remove_style_all(cont);
  lv_obj_set_size(cont, 64, 64);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* back = lv_label_create(cont);
  lv_label_set_text(back, LV_SYMBOL_WARNING);
  lv_obj_set_style_text_color(back, lv_color_black(), 0);
  lv_obj_set_style_text_font(back, &lv_font_montserrat_46, 0);
  lv_obj_align(back, LV_ALIGN_CENTER, 1, 1);

  lv_obj_t* front = lv_label_create(cont);
  lv_label_set_text(front, LV_SYMBOL_WARNING);
  lv_obj_set_style_text_color(front, color, 0);
  lv_obj_set_style_text_font(front, &lv_font_montserrat_40, 0);
  lv_obj_align(front, LV_ALIGN_CENTER, 0, 0);

  if (cb) lv_obj_add_event_cb(cont, cb, LV_EVENT_CLICKED, user);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, front);
  lv_anim_set_values(&a, LV_OPA_COVER, 0);
  lv_anim_set_time(&a, 450);
  lv_anim_set_playback_time(&a, 450);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_exec_cb(&a, _anim_set_opa);
  lv_anim_start(&a);

  return cont;
}

/* ===== MAIN ===== */
bool TempRegulator::loadSplashImage() {
  if (splash_img_dsc.data != nullptr) {
    return true;
  }

  File f = LittleFS.open("/splash.bin", FILE_READ);
  if (!f) {
    Serial.println("[Splash] LittleFS file /splash.bin not found");
    return false;
  }

  uint8_t header[4];
  if (f.read(header, sizeof(header)) != sizeof(header)) {
    Serial.println("[Splash] Failed to read splash header");
    return false;
  }

  const uint16_t width = static_cast<uint16_t>(header[0] | (header[1] << 8));
  const uint16_t height = static_cast<uint16_t>(header[2] | (header[3] << 8));
  if (width == 0 || height == 0) {
    Serial.println("[Splash] Invalid splash dimensions");
    return false;
  }

  const size_t expected = static_cast<size_t>(width) * static_cast<size_t>(height) * 2U;
  splash_img_buf.resize(expected);
  const size_t read_bytes = f.read(splash_img_buf.data(), expected);
  if (read_bytes != expected) {
    Serial.println("[Splash] Unexpected end of splash.bin");
    splash_img_buf.clear();
    return false;
  }
  f.close();

#if LVGL_VERSION_MAJOR >= 9
  splash_img_dsc = {};
  splash_img_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  splash_img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  splash_img_dsc.header.flags = 0;
  splash_img_dsc.header.w = width;
  splash_img_dsc.header.h = height;
  splash_img_dsc.header.stride = width * 2U;
  splash_img_dsc.data_size = expected;
  splash_img_dsc.data = splash_img_buf.data();
#else
  splash_img_dsc = {};
  splash_img_dsc.header.always_zero = 0;
  splash_img_dsc.header.w = width;
  splash_img_dsc.header.h = height;
  splash_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  splash_img_dsc.data_size = expected;
  splash_img_dsc.data = splash_img_buf.data();
#endif

  return true;
}

void TempRegulator::createMain() {
  scr_main = lv_obj_create(NULL);
  make_header(scr_main, "Главное меню");

  // список
  lv_obj_t* list = lv_list_create(scr_main);
  lv_obj_set_size(list, 300, 240 - HEADER_H - 16);
  place_below_header(list, 8);

  lv_obj_t* btnProfiles = list_add_btn_with_icon(list, LV_SYMBOL_PLAY,      "Температурные профили");
  lv_obj_t* btnManual   = list_add_btn_with_icon(list, LV_SYMBOL_EDIT,      "Ручной режим");
  lv_obj_t* btnSettings = list_add_btn_with_icon(list, LV_SYMBOL_SETTINGS,  "Настройки");
  lv_obj_t* btnInfo     = list_add_btn_with_icon(list, LV_SYMBOL_EYE_OPEN,  "Инфо");

  lv_obj_add_event_cb(btnProfiles, _main_profiles_cb,  LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(btnManual,   _main_manual_cb,    LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(btnSettings, _main_settings_cb,  LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(btnInfo,     _open_info_cb,      LV_EVENT_CLICKED, this);

  clear_encoder_group();
  ui_group = lv_group_create();
  lv_group_add_obj(ui_group, btnProfiles);
  lv_group_add_obj(ui_group, btnManual);
  lv_group_add_obj(ui_group, btnSettings);
  lv_group_add_obj(ui_group, btnInfo);
  lv_group_set_wrap(ui_group, true);
#if LVGL_VERSION_MAJOR >= 9
  lv_group_focus_obj(btnProfiles);
#else
  lv_group_focus_obj(ui_group, btnProfiles);
#endif
  set_encoder_group(ui_group);

  // предупреждающий треугольник в правом нижнем углу, если требуется
  if (alarm_active) {
    lv_obj_t* tri = make_warning_triangle(scr_main,
                                          lv_palette_main(LV_PALETTE_RED),
                                          _triangle_clicked_cb, this);
    lv_obj_align(tri, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
  } else if (!isCalibrated) {
    lv_obj_t* tri = make_warning_triangle(scr_main,
                                          lv_palette_main(LV_PALETTE_BLUE),
                                          _triangle_clicked_cb, this);
    lv_obj_align(tri, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
  }

  scr_load_smooth(scr_main);
}

void TempRegulator::createSplash() {
  lv_obj_t* scr = lv_obj_create(NULL);
  lv_obj_remove_style_all(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0xF6F1EA), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_scr_load(scr);

  if (loadSplashImage()) {
    lv_obj_t* img = lv_img_create(scr);
    lv_img_set_src(img, &splash_img_dsc);
    lv_obj_center(img);
  } else if (const auto* builtin_logo = logo_builtin_get_image()) {
    lv_obj_t* img = lv_img_create(scr);
    lv_img_set_src(img, builtin_logo);
    lv_obj_center(img);
  } else {
    lv_obj_t* frame = lv_obj_create(scr);
    lv_obj_remove_style_all(frame);
    lv_obj_set_size(frame, 200, 80);
    lv_obj_center(frame);
    lv_obj_set_style_bg_color(frame, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(frame, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_border_width(frame, 3, 0);
    lv_obj_set_style_radius(frame, 8, 0);

    lv_obj_t* lbl = lv_label_create(frame);
    lv_label_set_text(lbl, "AKTex");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl, lv_color_black(), 0);
    lv_obj_center(lbl);
  }

  lv_timer_t* tm = lv_timer_create(_splash_done_cb, 1000, this);
  lv_timer_set_repeat_count(tm, 1);
}

static void _main_profiles_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  s->ev = EVENT_TO_PROFILES;
}
static void _main_settings_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  s->ev = EVENT_TO_SETTINGS;
}
static void _main_manual_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  s->ev = EVENT_TO_MANUAL;
}

/* ===== Экран профилей ===== */
void TempRegulator::createProfiles() {
  loadTemperatureProfiles();

  scr_profiles = lv_obj_create(NULL);
  make_header(scr_profiles, "Температурные профили");

  lv_obj_t* list = lv_list_create(scr_profiles);
  lv_obj_set_size(list, 300, 240 - HEADER_H - 16);
  place_below_header(list, 8);

  profileButtonCount = 0;
  for (auto& btn : profileButtons) {
    btn = nullptr;
  }
  for (auto& idx : profileButtonToIndex) {
    idx = 0;
  }

  bool hasProfiles = false;
  for (size_t i = 0; i < kTemperatureProfileCount; ++i) {
    if (!profiles[i].isAvailable()) {
      continue;
    }
    hasProfiles = true;
    lv_obj_t* btn = list_add_btn_with_icon(list,
                                          LV_SYMBOL_PLAY,
                                          profiles[i].name().c_str(),
                                          false);
    lv_obj_add_event_cb(btn, _profiles_item_cb, LV_EVENT_CLICKED, this);
    profileButtons[profileButtonCount] = btn;
    profileButtonToIndex[profileButtonCount] = static_cast<uint8_t>(i);
    ++profileButtonCount;
  }

  if (!hasProfiles) {
    lv_obj_t* lbl = lv_label_create(list);
    lv_label_set_text(lbl, "Профили не найдены");
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
  }

  lv_obj_t* back = list_add_btn_with_icon(list, LV_SYMBOL_LEFT, "Назад", false);
  lv_obj_add_event_cb(back, _profiles_back_cb, LV_EVENT_CLICKED, this);

  clear_encoder_group();
  ui_group = lv_group_create();
  for (size_t i = 0; i < profileButtonCount; ++i) {
    if (profileButtons[i]) {
      lv_group_add_obj(ui_group, profileButtons[i]);
    }
  }
  lv_group_add_obj(ui_group, back);
  set_encoder_group(ui_group);

  scr_load_smooth(scr_profiles);
}

static void _profiles_item_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  if (!s) return;
  lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(ev));
  s->handleProfileSelection(target);
}
static void _profiles_back_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  s->onEnterReady();
}

/* ===== Настройки ===== */
void TempRegulator::createSettings() {
  scr_settings = lv_obj_create(NULL);
  make_header(scr_settings, "Настройки");

  lv_obj_t* list = lv_list_create(scr_settings);
  lv_obj_set_size(list, 300, 240 - HEADER_H - 16);
  place_below_header(list, 8);

  lv_obj_t* b1        = list_add_btn_with_icon(list, LV_SYMBOL_EDIT,  "Калибровка термопары");
  lv_obj_t* b2        = list_add_btn_with_icon(list, LV_SYMBOL_LOOP,  "Автонастройка PID");
  lv_obj_t* bAdvanced = list_add_btn_with_icon(list, "≡",             "Продвинутые настройки");
  lv_obj_t* bReset    = list_add_btn_with_icon(list, LV_SYMBOL_TRASH, "Сброс настроек");
  lv_obj_t* back      = list_add_btn_with_icon(list, LV_SYMBOL_LEFT,  "Назад в меню");

  lv_obj_add_event_cb(b1,        _settings_calib_cb,    LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(b2,        _settings_autotune_cb, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(bAdvanced, _settings_advanced_cb, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(bReset,    _settings_reset_cb,    LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(back,      _settings_back_cb,     LV_EVENT_CLICKED, this);

  clear_encoder_group();
  ui_group = lv_group_create();
  lv_group_add_obj(ui_group, b1);
  lv_group_add_obj(ui_group, b2);
  lv_group_add_obj(ui_group, bAdvanced);
  lv_group_add_obj(ui_group, bReset);
  lv_group_add_obj(ui_group, back);
  set_encoder_group(ui_group);

  scr_load_smooth(scr_settings);
}
static void _settings_calib_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  s->ev = EVENT_TO_CALIB;
}
static void _settings_need_cal_ok_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  _close_mbox_only_cb(ev);
  lv_async_call(_async_enter_settings, s);
}
static void _settings_autotune_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  if(!s->isCalibrated){
    lv_obj_t* m = lv_msgbox_create(NULL);
    lv_msgbox_add_text(m, "Требуется калибровка термопары");
    lv_obj_center(m);
    lv_obj_t* ok = lv_msgbox_add_footer_button(m, "ОК");
    lv_obj_add_event_cb(ok, _settings_need_cal_ok_cb, LV_EVENT_CLICKED, s);
    encoder_modal_take({ok});
    return;
  }
  s->ev = EVENT_TO_AUTOTUNE;
}
static void _settings_advanced_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  if(!s) return;
  s->createAdvancedSettings();
}
static void _settings_reset_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  s->createResetMenu();
}
static void _settings_back_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  s->onEnterReady();
}

static void _advanced_pid_cb(lv_event_t* ev){
  auto s = (TempRegulator*)lv_event_get_user_data(ev);
  if (!s) return;
  s->createPidCoeffsMenu();
}
static void _advanced_tc_cb(lv_event_t* ev){
  auto s = (TempRegulator*)lv_event_get_user_data(ev);
  if (!s) return;
  s->createThermoCoeffsMenu();
}
static void _advanced_back_cb(lv_event_t* ev){
  auto s = (TempRegulator*)lv_event_get_user_data(ev);
  if (!s) return;
  s->createSettings();
}

void TempRegulator::createAdvancedSettings() {
  lv_obj_t* scr = lv_obj_create(NULL);
  make_header(scr, "Продвинутые настройки");

  lv_obj_t* list = lv_list_create(scr);
  lv_obj_set_size(list, 300, 240 - HEADER_H - 16);
  place_below_header(list, 8);

  lv_obj_t* pid  = list_add_btn_with_icon(list, LV_SYMBOL_LOOP, "Изменение коэффициентов PID");
  lv_obj_t* tc   = list_add_btn_with_icon(list, LV_SYMBOL_EDIT, "Изменение коэффициентов термопары");
  lv_obj_t* back = list_add_btn_with_icon(list, LV_SYMBOL_LEFT, "Назад");

  lv_obj_add_event_cb(pid,  _advanced_pid_cb,  LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(tc,   _advanced_tc_cb,   LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(back, _advanced_back_cb, LV_EVENT_CLICKED, this);

  clear_encoder_group();
  ui_group = lv_group_create();
  lv_group_add_obj(ui_group, pid);
  lv_group_add_obj(ui_group, tc);
  lv_group_add_obj(ui_group, back);
  set_encoder_group(ui_group);

  scr_load_smooth(scr);
}

static void _pid_back_cb(lv_event_t* ev){
  auto s = (TempRegulator*)lv_event_get_user_data(ev);
  if (!s) return;
  s->createAdvancedSettings();
}
static void _pid_reset_confirm_cb(lv_event_t* ev);
static void _pid_reset_open_cb(lv_event_t* ev){
  auto s = (TempRegulator*)lv_event_get_user_data(ev);
  if (!s) return;
  lv_obj_t* m = lv_msgbox_create(NULL);
  lv_msgbox_add_text(m, "Вы увенерны");
  lv_obj_center(m);
  lv_obj_t* yes = lv_msgbox_add_footer_button(m, "Да");
  lv_obj_add_event_cb(yes, _pid_reset_confirm_cb, LV_EVENT_CLICKED, s);
  lv_obj_t* no = lv_msgbox_add_footer_button(m, "Нет");
  lv_obj_add_event_cb(no, _close_mbox_only_cb, LV_EVENT_CLICKED, nullptr);
  encoder_modal_take({yes, no});
}
static void _pid_reset_confirm_cb(lv_event_t* ev){
  auto s = (TempRegulator*)lv_event_get_user_data(ev);
  if (!s) return;
  s->resetPidCoeffsToDefaults();
  _close_mbox_only_cb(ev);
}
static void _pid_kp_plus_cb(lv_event_t* ev){
  auto s = (TempRegulator*)lv_event_get_user_data(ev);
  if (!s) return;
  s->adjustPidCoeffByIndex(0, 0.1);
}
static void _pid_kp_minus_cb(lv_event_t* ev){
  auto s = (TempRegulator*)lv_event_get_user_data(ev);
  if (!s) return;
  s->adjustPidCoeffByIndex(0, -0.1);
}
static void _pid_ki_plus_cb(lv_event_t* ev){
  auto s = (TempRegulator*)lv_event_get_user_data(ev);
  if (!s) return;
  s->adjustPidCoeffByIndex(1, 0.1);
}
static void _pid_ki_minus_cb(lv_event_t* ev){
  auto s = (TempRegulator*)lv_event_get_user_data(ev);
  if (!s) return;
  s->adjustPidCoeffByIndex(1, -0.1);
}
static void _pid_kd_plus_cb(lv_event_t* ev){
  auto s = (TempRegulator*)lv_event_get_user_data(ev);
  if (!s) return;
  s->adjustPidCoeffByIndex(2, 0.1);
}
static void _pid_kd_minus_cb(lv_event_t* ev){
  auto s = (TempRegulator*)lv_event_get_user_data(ev);
  if (!s) return;
  s->adjustPidCoeffByIndex(2, -0.1);
}

void TempRegulator::createPidCoeffsMenu() {
  lv_obj_t* scr = lv_obj_create(NULL);
  make_header(scr, "Изменение коэффициентов PID");

  lv_obj_t* cont = lv_obj_create(scr);
  lv_obj_remove_style_all(cont);
  lv_obj_set_size(cont, 300, 240 - HEADER_H - 16);
  place_below_header(cont, 8);
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_style_pad_gap(cont, 12, 0);

  lbl_pid_kp_val = lbl_pid_ki_val = lbl_pid_kd_val = nullptr;

  std::vector<lv_obj_t*> focus_items;
  focus_items.reserve(10);

  auto make_row = [&](const char* name,
                      lv_event_cb_t plus_cb,
                      lv_event_cb_t minus_cb,
                      lv_obj_t** value_lbl) {
    lv_obj_t* row = lv_obj_create(cont);
    lv_obj_remove_style_all(row);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_gap(row, 8, 0);
    lv_obj_set_flex_align(row,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t* lbl = lv_label_create(row);
    lv_label_set_text(lbl, name);
    lv_obj_set_flex_grow(lbl, 1);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_LEFT, 0);

    lv_obj_t* plus = make_icon_only_btn(row, LV_SYMBOL_PLUS);
    lv_obj_set_size(plus, 48, 36);
    lv_obj_add_event_cb(plus, plus_cb, LV_EVENT_CLICKED, this);
    focus_items.push_back(plus);

    lv_obj_t* minus = make_icon_only_btn(row, LV_SYMBOL_MINUS);
    lv_obj_set_size(minus, 48, 36);
    lv_obj_add_event_cb(minus, minus_cb, LV_EVENT_CLICKED, this);
    focus_items.push_back(minus);

    lv_obj_t* val = lv_label_create(row);
    lv_label_set_text(val, "0.0");
    lv_obj_set_width(val, 70);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
    if (value_lbl) {
      *value_lbl = val;
    }
  };

  make_row("Kp", _pid_kp_plus_cb, _pid_kp_minus_cb, &lbl_pid_kp_val);
  make_row("Ki", _pid_ki_plus_cb, _pid_ki_minus_cb, &lbl_pid_ki_val);
  make_row("Kd", _pid_kd_plus_cb, _pid_kd_minus_cb, &lbl_pid_kd_val);

  lv_obj_t* reset = make_btn_with_icon(cont, LV_SYMBOL_REFRESH, "Сбросить на заводские настройки");
  lv_obj_add_event_cb(reset, _pid_reset_open_cb, LV_EVENT_CLICKED, this);
  focus_items.push_back(reset);

  lv_obj_t* back = make_btn_with_icon(cont, LV_SYMBOL_LEFT, "Назад");
  lv_obj_add_event_cb(back, _pid_back_cb, LV_EVENT_CLICKED, this);
  focus_items.push_back(back);

  refreshPidCoeffLabels();

  clear_encoder_group();
  ui_group = lv_group_create();
  for (lv_obj_t* obj : focus_items) {
    if (obj) {
      lv_group_add_obj(ui_group, obj);
    }
  }
  set_encoder_group(ui_group);

  scr_load_smooth(scr);
}

static void _tc_back_cb(lv_event_t* ev){
  auto s = (TempRegulator*)lv_event_get_user_data(ev);
  if (!s) return;
  s->createAdvancedSettings();
}
static void _tc_reset_confirm_cb(lv_event_t* ev);
static void _tc_reset_open_cb(lv_event_t* ev){
  auto s = (TempRegulator*)lv_event_get_user_data(ev);
  if (!s) return;
  lv_obj_t* m = lv_msgbox_create(NULL);
  lv_msgbox_add_text(m, "Вы увенерны");
  lv_obj_center(m);
  lv_obj_t* yes = lv_msgbox_add_footer_button(m, "Да");
  lv_obj_add_event_cb(yes, _tc_reset_confirm_cb, LV_EVENT_CLICKED, s);
  lv_obj_t* no = lv_msgbox_add_footer_button(m, "Нет");
  lv_obj_add_event_cb(no, _close_mbox_only_cb, LV_EVENT_CLICKED, nullptr);
  encoder_modal_take({yes, no});
}
static void _tc_reset_confirm_cb(lv_event_t* ev){
  auto s = (TempRegulator*)lv_event_get_user_data(ev);
  if (!s) return;
  s->resetThermoCoeffsToDefaults();
  _close_mbox_only_cb(ev);
}
static void _tc_kl_plus_cb(lv_event_t* ev){
  auto s = (TempRegulator*)lv_event_get_user_data(ev);
  if (!s) return;
  s->adjustThermoCoeffByIndex(0, 0.1f);
}
static void _tc_kl_minus_cb(lv_event_t* ev){
  auto s = (TempRegulator*)lv_event_get_user_data(ev);
  if (!s) return;
  s->adjustThermoCoeffByIndex(0, -0.1f);
}
static void _tc_kc_plus_cb(lv_event_t* ev){
  auto s = (TempRegulator*)lv_event_get_user_data(ev);
  if (!s) return;
  s->adjustThermoCoeffByIndex(1, 0.1f);
}
static void _tc_kc_minus_cb(lv_event_t* ev){
  auto s = (TempRegulator*)lv_event_get_user_data(ev);
  if (!s) return;
  s->adjustThermoCoeffByIndex(1, -0.1f);
}

void TempRegulator::createThermoCoeffsMenu() {
  lv_obj_t* scr = lv_obj_create(NULL);
  make_header(scr, "Изменение коэффициентов термопары");

  lv_obj_t* cont = lv_obj_create(scr);
  lv_obj_remove_style_all(cont);
  lv_obj_set_size(cont, 300, 240 - HEADER_H - 16);
  place_below_header(cont, 8);
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_style_pad_gap(cont, 12, 0);

  lbl_tc_kl_val = lbl_tc_kc_val = nullptr;

  std::vector<lv_obj_t*> focus_items;
  focus_items.reserve(8);

  auto make_row = [&](const char* name,
                      lv_event_cb_t plus_cb,
                      lv_event_cb_t minus_cb,
                      lv_obj_t** value_lbl) {
    lv_obj_t* row = lv_obj_create(cont);
    lv_obj_remove_style_all(row);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_gap(row, 8, 0);
    lv_obj_set_flex_align(row,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t* lbl = lv_label_create(row);
    lv_label_set_text(lbl, name);
    lv_obj_set_flex_grow(lbl, 1);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_LEFT, 0);

    lv_obj_t* plus = make_icon_only_btn(row, LV_SYMBOL_PLUS);
    lv_obj_set_size(plus, 48, 36);
    lv_obj_add_event_cb(plus, plus_cb, LV_EVENT_CLICKED, this);
    focus_items.push_back(plus);

    lv_obj_t* minus = make_icon_only_btn(row, LV_SYMBOL_MINUS);
    lv_obj_set_size(minus, 48, 36);
    lv_obj_add_event_cb(minus, minus_cb, LV_EVENT_CLICKED, this);
    focus_items.push_back(minus);

    lv_obj_t* val = lv_label_create(row);
    lv_label_set_text(val, "0.0");
    lv_obj_set_width(val, 70);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
    if (value_lbl) {
      *value_lbl = val;
    }
  };

  make_row("Kl", _tc_kl_plus_cb, _tc_kl_minus_cb, &lbl_tc_kl_val);
  make_row("Kc", _tc_kc_plus_cb, _tc_kc_minus_cb, &lbl_tc_kc_val);

  lv_obj_t* reset = make_btn_with_icon(cont, LV_SYMBOL_REFRESH, "Сбросить на заводские настройки");
  lv_obj_add_event_cb(reset, _tc_reset_open_cb, LV_EVENT_CLICKED, this);
  focus_items.push_back(reset);

  lv_obj_t* back = make_btn_with_icon(cont, LV_SYMBOL_LEFT, "Назад");
  lv_obj_add_event_cb(back, _tc_back_cb, LV_EVENT_CLICKED, this);
  focus_items.push_back(back);

  refreshThermoCoeffLabels();

  clear_encoder_group();
  ui_group = lv_group_create();
  for (lv_obj_t* obj : focus_items) {
    if (obj) {
      lv_group_add_obj(ui_group, obj);
    }
  }
  set_encoder_group(ui_group);

  scr_load_smooth(scr);
}

void TempRegulator::refreshPidCoeffLabels() {
  if (lbl_pid_kp_val) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%.1f", pid_kp);
    lv_label_set_text(lbl_pid_kp_val, buf);
  }
  if (lbl_pid_ki_val) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%.1f", pid_ki);
    lv_label_set_text(lbl_pid_ki_val, buf);
  }
  if (lbl_pid_kd_val) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%.1f", pid_kd);
    lv_label_set_text(lbl_pid_kd_val, buf);
  }
}

void TempRegulator::refreshThermoCoeffLabels() {
  if (lbl_tc_kl_val) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%.1f", (double)slope);
    lv_label_set_text(lbl_tc_kl_val, buf);
  }
  if (lbl_tc_kc_val) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%.1f", (double)offset);
    lv_label_set_text(lbl_tc_kc_val, buf);
  }
}

void TempRegulator::adjustPidCoeffByIndex(int idx, double delta) {
  double* coeffs[] = {&pid_kp, &pid_ki, &pid_kd};
  if (idx < 0 || idx >= static_cast<int>(sizeof(coeffs) / sizeof(coeffs[0]))) {
    return;
  }
  double* target = coeffs[idx];
  double value = *target + delta;
  value = std::round(value * 10.0) / 10.0;
  *target = value;
  refreshPidCoeffLabels();
  saveNVS();
  pid.setCoeffs(pid_kp, pid_ki, pid_kd);
}

void TempRegulator::adjustThermoCoeffByIndex(int idx, float delta) {
  float* coeffs[] = {&slope, &offset};
  if (idx < 0 || idx >= static_cast<int>(sizeof(coeffs) / sizeof(coeffs[0]))) {
    return;
  }
  float* target = coeffs[idx];
  double value = static_cast<double>(*target) + static_cast<double>(delta);
  value = std::round(value * 10.0) / 10.0;
  *target = static_cast<float>(value);
  refreshThermoCoeffLabels();
  saveNVS();
}

void TempRegulator::resetPidCoeffsToDefaults() {
  pid_kp = 2.0;
  pid_ki = 5.0;
  pid_kd = 1.0;
  refreshPidCoeffLabels();
  saveNVS();
  pid.setCoeffs(pid_kp, pid_ki, pid_kd);
}

void TempRegulator::resetThermoCoeffsToDefaults() {
  slope = 1.0f;
  offset = 0.0f;
  refreshThermoCoeffLabels();
  saveNVS();
}
/* ===== Меню сбросов ===== */
void TempRegulator::createResetMenu() {
  lv_obj_t* scr_reset = lv_obj_create(NULL);
  make_header(scr_reset, "Сброс настроек");

  lv_obj_t* list = lv_list_create(scr_reset);
  lv_obj_set_size(list, 300, 240 - HEADER_H - 16);
  place_below_header(list, 8);

  lv_obj_t* r_touch = list_add_btn_with_icon(list, LV_SYMBOL_REFRESH, "Сброс калибровки экрана");
  lv_obj_t* r_tc    = list_add_btn_with_icon(list, LV_SYMBOL_TRASH,   "Сброс термопары (kc/kl)");
  lv_obj_t* r_pid   = list_add_btn_with_icon(list, LV_SYMBOL_LOOP,    "Сброс PID (Kp, Ki, Kd)");
  lv_obj_t* back    = list_add_btn_with_icon(list, LV_SYMBOL_LEFT,    "Назад");

  lv_obj_add_event_cb(r_touch, _reset_touch_open_confirm_cb, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(r_tc,    _reset_tc_open_confirm_cb,    LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(r_pid,   _reset_pid_open_confirm_cb,   LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(back,    _reset_back_cb,               LV_EVENT_CLICKED, this);

  clear_encoder_group();
  ui_group = lv_group_create();
  lv_group_add_obj(ui_group, r_touch);
  lv_group_add_obj(ui_group, r_tc);
  lv_group_add_obj(ui_group, r_pid);
  lv_group_add_obj(ui_group, back);
  set_encoder_group(ui_group);

  scr_load_smooth(scr_reset);
}
static void _open_confirm(TempRegulator* s, const char* txt, lv_event_cb_t ok_cb){
  lv_obj_t* m = lv_msgbox_create(NULL);
  lv_msgbox_add_text(m, txt);
  lv_obj_center(m);
  lv_obj_t* cancel = lv_msgbox_add_footer_button(m, "Отмена");
  lv_obj_t* ok     = lv_msgbox_add_footer_button(m, "Сбросить");
  lv_obj_add_event_cb(cancel, _close_mbox_only_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(ok, ok_cb, LV_EVENT_CLICKED, s);
  encoder_modal_take({cancel, ok});
}

static void _open_exit_confirm(TempRegulator* s) {
  lv_obj_t* m = lv_msgbox_create(NULL);
  lv_msgbox_add_text(m, "Вы уверены что хотите выйти?");
  lv_obj_center(m);
  lv_obj_t* yes = lv_msgbox_add_footer_button(m, "Да");
  lv_obj_t* no  = lv_msgbox_add_footer_button(m, "Нет");
  lv_obj_add_event_cb(yes, _exit_confirm_yes_cb, LV_EVENT_CLICKED, s);
  lv_obj_add_event_cb(no, _exit_confirm_no_cb, LV_EVENT_CLICKED, nullptr);
  encoder_modal_take({yes, no});
}

static void _exit_confirm_yes_cb(lv_event_t* ev) {
  auto s = (TempRegulator*)lv_event_get_user_data(ev);
  _close_mbox_only_cb(ev);
  if (s) {
    s->ev = EVENT_STOP;
  }
}

static void _exit_confirm_no_cb(lv_event_t* ev) {
  _close_mbox_only_cb(ev);
}
static void _ok_reset_touch_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  _close_mbox_only_cb(ev);
  lv_async_call(_async_do_reset_touch, s);
}
static void _ok_reset_tc_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  _close_mbox_only_cb(ev);
  lv_async_call(_async_do_reset_tc, s);
}
static void _ok_reset_pid_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  _close_mbox_only_cb(ev);
  lv_async_call(_async_do_reset_pid, s);
}
static void _reset_touch_open_confirm_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  _open_confirm(s, "Сбросить калибровку экрана?\nБудет запущен мастер калибровки.", _ok_reset_touch_cb);
}
static void _reset_tc_open_confirm_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  _open_confirm(s, "Сбросить параметры термопары?\nБудут сброшены kl/kc и флаг калибровки.", _ok_reset_tc_cb);
}
static void _reset_pid_open_confirm_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  _open_confirm(s, "Сбросить параметры PID?\nБудут установлены значения по умолчанию.", _ok_reset_pid_cb);
}
static void _reset_back_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  s->onEnterSettings();
}

/* ===== Touch calib helpers ===== */
const lv_point_t kCrossPts[4] = { {20,40}, {300,40}, {20,220}, {300,220} };

static lv_obj_t* make_cross(lv_obj_t* parent, int x, int y) {
  static lv_point_precise_t hline[2];
  static lv_point_precise_t vline[2];

  lv_obj_t* cont = lv_obj_create(parent);
  lv_obj_remove_style_all(cont);
  lv_obj_set_size(cont, 320, 240);
  lv_obj_align(cont, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t* l1 = lv_line_create(cont);
  hline[0].x = x-12; hline[0].y = y;
  hline[1].x = x+12; hline[1].y = y;
  lv_line_set_points(l1, hline, 2);
  lv_obj_set_style_line_width(l1, 3, 0);
  lv_obj_set_style_line_color(l1, lv_color_black(), 0);

  lv_obj_t* l2 = lv_line_create(cont);
  vline[0].x = x; vline[0].y = y-12;
  vline[1].x = x; vline[1].y = y+12;
  lv_line_set_points(l2, vline, 2);
  lv_obj_set_style_line_width(l2, 3, 0);
  lv_obj_set_style_line_color(l2, lv_color_black(), 0);

  return cont;
}

/* ===== Тач-калибровка: экран ===== */
void TempRegulator::createTouchCalib() {
  clear_encoder_group();
  scr_tcal = lv_obj_create(NULL);
  make_header(scr_tcal, "Калибровка");

  lbl_tcal = lv_label_create(scr_tcal);
  lv_label_set_text(lbl_tcal, "Коснитесь крестика");
  lv_obj_align(lbl_tcal, LV_ALIGN_CENTER, 0, -20);

  tcal_idx   = 0;
  t_pressed  = false;
  t_press_t0 = 0;

  if (cross) { lv_obj_del(cross); cross = nullptr; }
  cross = make_cross(scr_tcal, kCrossPts[0].x, kCrossPts[0].y);

  lv_scr_load(scr_tcal);
}
void TempRegulator::tcal_next_target() {
  if (cross) lv_obj_del(cross);
  if (tcal_idx < 4) {
    cross = make_cross(scr_tcal, kCrossPts[tcal_idx].x, kCrossPts[tcal_idx].y);
    char b[48]; snprintf(b,sizeof(b),"Точка %u/4", (unsigned)(tcal_idx+1));
    lv_label_set_text(lbl_tcal, b);
  }
}
void TempRegulator::tcal_finish_and_save() {
  int dx_h = abs((int)rawx[1] - (int)rawx[0]);
  int dy_h = abs((int)rawy[1] - (int)rawy[0]);
  g_touch_swap_axes = (dy_h > dx_h);

  uint16_t left_raw, right_raw, top_raw, bottom_raw;
  if (!g_touch_swap_axes) {
    left_raw   = (uint16_t)(((uint32_t)rawx[0] + rawx[2]) / 2);
    right_raw  = (uint16_t)(((uint32_t)rawx[1] + rawx[3]) / 2);
    top_raw    = (uint16_t)(((uint32_t)rawy[0] + rawy[1]) / 2);
    bottom_raw = (uint16_t)(((uint32_t)rawy[2] + rawy[3]) / 2);
  } else {
    left_raw   = (uint16_t)(((uint32_t)rawy[0] + rawy[2]) / 2);
    right_raw  = (uint16_t)(((uint32_t)rawy[1] + rawy[3]) / 2);
    top_raw    = (uint16_t)(((uint32_t)rawx[0] + rawx[1]) / 2);
    bottom_raw = (uint16_t)(((uint32_t)rawx[2] + rawx[3]) / 2);
  }

  uint16_t dx = (uint16_t)abs((int)right_raw - (int)left_raw);
  uint16_t dy = (uint16_t)abs((int)bottom_raw - (int)top_raw);

  if (dx < 200 || dy < 200) {
    msgbox("Слишком маленький диапазон.\nПовторите калибровку.");
    tcs = TCS_1; tcal_idx=0;
    tcal_next_target();
    return;
  }

  g_tx_min = left_raw;  g_tx_max = right_raw;
  g_ty_min = top_raw;   g_ty_max = bottom_raw;
  g_touch_calibrated = true;
  saveNVS();
  beep(120);
  msgbox("Калибровка завершена");

  ev = EVENT_INIT_OK;
  state = STATE_INIT;
}
/* ===== Калибровка датчика — шаги ===== */
void TempRegulator::createCalibS1() {
  scr_cal_s1 = lv_obj_create(NULL);
  make_header(scr_cal_s1, "Калибровка 1/2 (ОС)");

  lv_obj_t* row = lv_obj_create(scr_cal_s1);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, 300, 60);
  place_below_header(row, 10);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_gap(row, 8, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);

  lv_obj_t* lbl_tip = lv_label_create(row);
  lv_label_set_text(lbl_tip, "Введите температуру ОС");
  lv_obj_set_flex_grow(lbl_tip, 1);
  lv_obj_set_style_text_align(lbl_tip, LV_TEXT_ALIGN_LEFT, 0);

  lbl_cal_val = lv_label_create(scr_cal_s1);
  char b[32]; snprintf(b, sizeof(b), "%.1f °C", cal_temp1);
  lv_label_set_text(lbl_cal_val, b);
  place_below_header(lbl_cal_val, 74);

  ensure_icon_m16_style();

  lv_obj_t* btn_plus = lv_btn_create(scr_cal_s1);
  lv_obj_set_size(btn_plus, 50, 26);
  lv_obj_align(btn_plus, LV_ALIGN_CENTER, -40, 16);
  { lv_obj_t* icon = lv_label_create(btn_plus); lv_obj_add_style(icon, &g_icon_m16_style, 0); lv_label_set_text(icon, LV_SYMBOL_PLUS); lv_obj_center(icon); }
  lv_obj_add_event_cb(btn_plus, _cal1_plus_cb, LV_EVENT_CLICKED, this);

  lv_obj_t* btn_minus = lv_btn_create(scr_cal_s1);
  lv_obj_set_size(btn_minus, 50, 26);
  lv_obj_align(btn_minus, LV_ALIGN_CENTER, 40, 16);
  { lv_obj_t* icon = lv_label_create(btn_minus); lv_obj_add_style(icon, &g_icon_m16_style, 0); lv_label_set_text(icon, LV_SYMBOL_MINUS); lv_obj_center(icon); }
  lv_obj_add_event_cb(btn_minus, _cal1_minus_cb, LV_EVENT_CLICKED, this);

  lv_obj_t* back = lv_btn_create(scr_cal_s1);
  lv_obj_set_size(back, 120, 40);
  lv_obj_align(back, LV_ALIGN_BOTTOM_LEFT, 8, -8);
  { lv_obj_t* lbl = lv_label_create(back); lv_label_set_text(lbl, "Назад"); lv_obj_center(lbl); }
  lv_obj_add_event_cb(back, _cal1_back_cb, LV_EVENT_CLICKED, this);

  btn_ok = lv_btn_create(scr_cal_s1);
  lv_obj_set_size(btn_ok, 120, 40);
  lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
  { lv_obj_t* lbl = lv_label_create(btn_ok); lv_label_set_text(lbl, "Далее"); lv_obj_center(lbl); }
  lv_obj_add_event_cb(btn_ok, _cal1_ok_cb, LV_EVENT_CLICKED, this);

  clear_encoder_group();
  ui_group = lv_group_create();
  lv_group_add_obj(ui_group, btn_plus);
  lv_group_add_obj(ui_group, btn_minus);
  lv_group_add_obj(ui_group, back);
  lv_group_add_obj(ui_group, btn_ok);
  set_encoder_group(ui_group);

  scr_load_smooth(scr_cal_s1);
}
static void _cal1_plus_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  s->cal_temp1 += 0.5f; if (s->cal_temp1 > 60) s->cal_temp1 = 60;
  char bb[32]; snprintf(bb, sizeof(bb), "%.1f °C", s->cal_temp1);
  lv_label_set_text(s->lbl_cal_val, bb);
}
static void _cal1_minus_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  s->cal_temp1 -= 0.5f; if (s->cal_temp1 < -20) s->cal_temp1 = -20;
  char bb[32]; snprintf(bb, sizeof(bb), "%.1f °C", s->cal_temp1);
  lv_label_set_text(s->lbl_cal_val, bb);
}
static void _cal1_back_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  s->onEnterSettings();
}
static void _cal1_ok_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  s->cst = CAL_STEP1_MEASURE; s->cal_t0 = millis();
  s->createCalibS2();
}

void TempRegulator::createCalibS2() {
  scr_cal_s2 = lv_obj_create(NULL);
  make_header(scr_cal_s2, "Калибровка 2/2 (Кипение)");
  lbl_cal_val = lv_label_create(scr_cal_s2);
  lv_label_set_text(lbl_cal_val, "АЦП: ----");
  place_below_header(lbl_cal_val, 10);

  lv_obj_t* back = make_btn_with_icon(scr_cal_s2, LV_SYMBOL_LEFT, "Назад");
  lv_obj_set_size(back, 120, 40); lv_obj_align(back, LV_ALIGN_BOTTOM_LEFT, 8, -8);
  lv_obj_add_event_cb(back, _cal2_back_cb, LV_EVENT_CLICKED, this);

  btn_ok = make_btn_with_icon(scr_cal_s2, LV_SYMBOL_OK, "ОК");
  lv_obj_set_size(btn_ok, 120, 40); lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
  lv_obj_add_event_cb(btn_ok, _cal2_ok_cb, LV_EVENT_CLICKED, this);

  scr_load_smooth(scr_cal_s2);
}
static void _cal2_back_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  s->cst=CAL_STEP1_INPUT_AMBIENT;
  s->createCalibS1();
}
static void _cal2_ok_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  if (s->stable){
    s->cst = CAL_STEP2_MEASURE;
    s->cal_t0 = millis();
  } else {
    s->onEnterReady();
    lv_async_call(_async_open_profiles, s);
  }
}

/* ===== Сообщение калибровки ===== */
void TempRegulator::createCalibMsg(const char* text) {
  lv_obj_t* scr = lv_obj_create(NULL);
  make_header(scr, "Калибровка");
  lv_obj_t* l=lv_label_create(scr); lv_label_set_text(l, text);
  place_below_header(l, 10);

  lv_obj_t* b = make_btn_with_icon(scr, LV_SYMBOL_LEFT, "Меню");
  lv_obj_set_size(b, 160, 40); lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_add_event_cb(b, _settings_back_cb, LV_EVENT_CLICKED, this);

  scr_load_smooth(scr);
}

/* ===== Автонастройка UI ===== */
void TempRegulator::createAtSetup() {
  scr_at_setup = lv_obj_create(NULL);
  make_header(scr_at_setup, "Автонастройка PID");
  lbl_at_cur = lv_label_create(scr_at_setup);
  char b[48]; snprintf(b,sizeof(b),"Цель: %.0f °C", at_target);
  lv_label_set_text(lbl_at_cur,b); place_below_header(lbl_at_cur, 10);

  lv_obj_t* back = make_btn_with_icon(scr_at_setup, LV_SYMBOL_LEFT, "Назад");
  lv_obj_set_size(back, 120, 40); lv_obj_align(back, LV_ALIGN_BOTTOM_LEFT, 8, -8);
  lv_obj_add_event_cb(back, _settings_back_cb, LV_EVENT_CLICKED, this);

  lv_obj_t* next = make_btn_with_icon(scr_at_setup, LV_SYMBOL_RIGHT, "Далее");
  lv_obj_set_size(next, 120, 40); lv_obj_align(next, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
  lv_obj_add_event_cb(next, _at_setup_next_cb, LV_EVENT_CLICKED, this);

  scr_load_smooth(scr_at_setup);
}
void TempRegulator::createAtConfirm() {
  scr_at_confirm = lv_obj_create(NULL);
  make_header(scr_at_confirm, "Подтверждение");
  char b[96]; snprintf(b,sizeof(b),"Нагрев будет автоматическим.\nЦель: %.0f °C", at_target);
  lv_obj_t* l=lv_label_create(scr_at_confirm); lv_label_set_text(l,b); place_below_header(l, 10);

  lv_obj_t* no = make_btn_with_icon(scr_at_confirm, LV_SYMBOL_LEFT, "Нет");
  lv_obj_set_size(no, 120, 40); lv_obj_align(no, LV_ALIGN_BOTTOM_LEFT, 8, -8);
  lv_obj_add_event_cb(no, _at_confirm_no_cb, LV_EVENT_CLICKED, this);

  lv_obj_t* yes = make_btn_with_icon(scr_at_confirm, LV_SYMBOL_OK, "Да");
  lv_obj_set_size(yes, 120, 40); lv_obj_align(yes, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
  lv_obj_add_event_cb(yes, _at_confirm_yes_cb, LV_EVENT_CLICKED, this);

  scr_load_smooth(scr_at_confirm);
}
void TempRegulator::createAtRun() {
  scr_at_run = lv_obj_create(NULL);
  make_header(scr_at_run, "Автонастройка...");
  lbl_at_cur  = lv_label_create(scr_at_run); lv_label_set_text(lbl_at_cur, "T: ---- °C"); place_below_header(lbl_at_cur, 6);
  lbl_at_time = lv_label_create(scr_at_run); lv_label_set_text(lbl_at_time,"t: 0 с");   lv_obj_align(lbl_at_time, LV_ALIGN_CENTER, 0,  16);

  lv_obj_t* abortb = make_btn_with_icon(scr_at_run, LV_SYMBOL_WARNING, "Аварийная остановка");
  lv_obj_set_size(abortb, 180, 40); lv_obj_align(abortb, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_add_event_cb(abortb, _at_abort_cb, LV_EVENT_CLICKED, this);

  scr_load_smooth(scr_at_run);
}
static void _at_setup_next_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  if(!s->isCalibrated){ s->msgbox("Требуется калибровка"); return; }
  if(s->at_target<AT_MIN_TARGET_C || s->at_target>AT_MAX_TARGET_C){ s->msgbox("Цель 40..500 °C"); return; }
  s->createAtConfirm();
}
static void _at_confirm_no_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  s->createAtSetup();
}
static void _at_confirm_yes_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  s->startAutotune();
}
static void _at_abort_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  s->atst=AT_ABORT;
}

/* ===== Вспомогательные для кнопки Стоп/Пуск ===== */
static void set_btn_icon_text(lv_obj_t* btn, const char* sym, const char* text) {
  if (!btn) return;
  lv_obj_t* icon = lv_obj_get_child(btn, 0);
  lv_obj_t* lbl  = lv_obj_get_child(btn, 1);
  if (icon) lv_label_set_text(icon, sym);
  if (lbl)  lv_label_set_text(lbl,  text);
}

void TempRegulator::updateHeatButtonsUI() {
  const char* sym  = heating ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY;
  const char* text = heating ? "Стоп" : "Пуск";
  if (btn_work_heat)   set_btn_icon_text(btn_work_heat,   sym, text);
  if (btn_manual_heat) set_btn_icon_text(btn_manual_heat, sym, text);
}

void TempRegulator::startHeat() {
  heating = true;
  updateHeatButtonsUI();
}
void TempRegulator::stopHeat() {
  heating = false;
  ssr_power_0_255 = 0;
  digitalWrite(SSR_CONTROL_PIN, LOW);
  updateHeatButtonsUI();
}
void TempRegulator::setHeating(bool on) {
  if (on) startHeat(); else stopHeat();
}

/* ===== WORK (профиль — как список) ===== */
void TempRegulator::createWork() {
  scr_work = lv_obj_create(NULL);
  String header = "Профиль работа";
  if (activeProfileIndex >= 0 && activeProfileIndex < static_cast<int8_t>(kTemperatureProfileCount)) {
    if (profiles[activeProfileIndex].isAvailable()) {
      header = "Профиль: " + profiles[activeProfileIndex].name();
    }
  }
  make_header(scr_work, header.c_str());

  const int bottom_area = 52;
  lv_obj_t* list = lv_obj_create(scr_work);
  lv_obj_remove_style_all(list);
  lv_obj_set_size(list, 300, 240 - HEADER_H - 8 - bottom_area);
  place_below_header(list, 4);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(list, 2, 0);
  lv_obj_set_style_pad_left(list, 0, 0);
  lv_obj_set_style_pad_right(list, 0, 0);
  lv_obj_set_style_pad_top(list, 0, 0);
  lv_obj_set_style_pad_bottom(list, 0, 0);
  lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);

  lbl_work_cur = lbl_work_sp = lbl_work_pow = nullptr;
  make_kv_row(list, "Температура",       "----", "°C", &lbl_work_cur);
  make_kv_row(list, "Заданная темп.",    "----", "°C", &lbl_work_sp);
  make_kv_row(list, "Мощность нагрева",  "----", "%",  &lbl_work_pow);

  // ОДНА кнопка Стоп/Пуск + Назад
  btn_work_heat = make_btn_with_icon(scr_work, LV_SYMBOL_STOP, "Стоп", true);
  lv_obj_set_size(btn_work_heat, 120, 36);
  lv_obj_align(btn_work_heat, LV_ALIGN_BOTTOM_LEFT, 6, -6);
  lv_obj_add_event_cb(btn_work_heat, _heat_toggle_cb, LV_EVENT_CLICKED, this);

  lv_obj_t* btn_back = make_btn_with_icon(scr_work, LV_SYMBOL_LEFT, "Назад", true);
  lv_obj_set_size(btn_back, 120, 36);
  lv_obj_align(btn_back, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
  lv_obj_add_event_cb(btn_back, _work_back_cb, LV_EVENT_CLICKED, this);

  clear_encoder_group();
  ui_group = lv_group_create();
  lv_group_add_obj(ui_group, btn_work_heat);
  lv_group_add_obj(ui_group, btn_back);
  set_encoder_group(ui_group);

  updateHeatButtonsUI();
  scr_load_smooth(scr_work);
}

static void _work_back_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  if (!s) {
    return;
  }
  _open_exit_confirm(s);
}

/* ===== Ручной режим (как список) ===== */
void TempRegulator::createManual() {
  lv_obj_t* scr = lv_obj_create(NULL);
  make_header(scr, "Ручной режим");

  const int bottom_area = 84;
  lv_obj_t* list = lv_obj_create(scr);
  lv_obj_remove_style_all(list);
  lv_obj_set_size(list, 300, 240 - HEADER_H - 8 - bottom_area);
  place_below_header(list, 4);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(list, 2, 0);
  lv_obj_set_style_pad_left(list, 0, 0);
  lv_obj_set_style_pad_right(list, 0, 0);
  lv_obj_set_style_pad_top(list, 0, 0);
  lv_obj_set_style_pad_bottom(list, 0, 0);
  lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);

  lbl_man_cur = lbl_man_sp = nullptr;
  make_kv_row(list, "Текущая темп.", "----", "°C", &lbl_man_cur);

  char buf[24]; snprintf(buf, sizeof(buf), "%.1f", getTargetC());
  make_kv_row(list, "Целевая темп.", buf, "°C", &lbl_man_sp);

  // – / +
  ensure_icon_m16_style();
  lv_obj_t* minus = lv_btn_create(scr);
  lv_obj_set_size(minus, 60, 36);
  lv_obj_align(minus, LV_ALIGN_BOTTOM_LEFT, 8, -44);
  { auto* ic = lv_label_create(minus); lv_obj_add_style(ic, &g_icon_m16_style, 0); lv_label_set_text(ic, LV_SYMBOL_MINUS); lv_obj_center(ic); }
  lv_obj_add_event_cb(minus, _manual_minus_cb, LV_EVENT_CLICKED, this);
  apply_focus_style(minus, true);

  lv_obj_t* plus = lv_btn_create(scr);
  lv_obj_set_size(plus, 60, 36);
  lv_obj_align(plus, LV_ALIGN_BOTTOM_LEFT, 76, -44);
  { auto* ic = lv_label_create(plus); lv_obj_add_style(ic, &g_icon_m16_style, 0); lv_label_set_text(ic, LV_SYMBOL_PLUS); lv_obj_center(ic); }
  lv_obj_add_event_cb(plus, _manual_plus_cb, LV_EVENT_CLICKED, this);
  apply_focus_style(plus, true);

  // ОДНА кнопка Стоп/Пуск + Назад
  btn_manual_heat = make_btn_with_icon(scr, LV_SYMBOL_STOP, "Стоп", true);
  lv_obj_set_size(btn_manual_heat, 120, 36);
  lv_obj_align(btn_manual_heat, LV_ALIGN_BOTTOM_LEFT, 8, -6);
  lv_obj_add_event_cb(btn_manual_heat, _heat_toggle_cb, LV_EVENT_CLICKED, this);

  lv_obj_t* back = make_btn_with_icon(scr, LV_SYMBOL_LEFT, "Назад", true);
  lv_obj_set_size(back, 120, 40);
  lv_obj_align(back, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
  lv_obj_add_event_cb(back, _manual_back_cb, LV_EVENT_CLICKED, this);

  clear_encoder_group();
  ui_group = lv_group_create();
  lv_group_add_obj(ui_group, minus);
  lv_group_add_obj(ui_group, plus);
  lv_group_add_obj(ui_group, btn_manual_heat);
  lv_group_add_obj(ui_group, back);
  set_encoder_group(ui_group);

  updateHeatButtonsUI();
  scr_load_smooth(scr);
}

/* Универсальный тоггл «Стоп/Пуск» */
static void _heat_toggle_cb(lv_event_t* ev) {
  auto s = (TempRegulator*)lv_event_get_user_data(ev);
  s->setHeating(!s->heating);
}

static void _manual_minus_cb(lv_event_t* ev){
  auto s = (TempRegulator*)lv_event_get_user_data(ev);
  s->adjustTargetC(-1.0f);
  if (s->lbl_man_sp){
    char b[24]; snprintf(b, sizeof(b), "%.1f", s->getTargetC());
    lv_label_set_text(s->lbl_man_sp, b);
  }
}
static void _manual_plus_cb(lv_event_t* ev){
  auto s = (TempRegulator*)lv_event_get_user_data(ev);
  s->adjustTargetC(+1.0f);
  if (s->lbl_man_sp){
    char b[24]; snprintf(b, sizeof(b), "%.1f", s->getTargetC());
    lv_label_set_text(s->lbl_man_sp, b);
  }
}
static void _manual_back_cb(lv_event_t* ev){
  auto s=(TempRegulator*)lv_event_get_user_data(ev);
  if (!s) {
    return;
  }
  _open_exit_confirm(s);
}

static void _alarm_ok_cb(lv_event_t* ev){
  auto s = (TempRegulator*)lv_event_get_user_data(ev);
  _close_mbox_only_cb(ev);
  s->clearAlarm();
  digitalWrite(SSR_CONTROL_PIN, LOW);
  s->onEnterReady();
}

/* ===== Touch Test ===== */
void TempRegulator::createTouchTest() {
  clear_encoder_group();
  lv_obj_t* scr = lv_obj_create(NULL);
  make_header(scr, "Проверка касания");

  lv_obj_t* tip = lv_label_create(scr);
  lv_label_set_text(tip, "Коснитесь центральной кнопки");
  lv_obj_set_width(tip, 300);
  lv_label_set_long_mode(tip, LV_LABEL_LONG_WRAP);
  place_below_header(tip, 8);

  lv_obj_t* bg = lv_btn_create(scr);
  lv_obj_remove_style_all(bg);
  lv_obj_set_size(bg, 320, 240 - HEADER_H);
  lv_obj_align(bg, LV_ALIGN_TOP_LEFT, 0, HEADER_H);
  lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(bg, _touchtest_bg_cb, LV_EVENT_CLICKED, this);

  lv_obj_t* btn = lv_btn_create(scr);
  lv_obj_set_size(btn, 140, 60);
  lv_obj_center(btn);
  { lv_obj_t* lbl = lv_label_create(btn); lv_label_set_text(lbl, "Ок"); lv_obj_center(lbl); }
  lv_obj_add_event_cb(btn, _touchtest_btn_cb, LV_EVENT_CLICKED, this);

  scr_load_smooth(scr);
}
static void _touchtest_btn_cb(lv_event_t* ev){
  lv_event_stop_bubbling(ev);
  auto s = (TempRegulator*)lv_event_get_user_data(ev);
  lv_indev_t* indev = lv_event_get_indev(ev);
  bool is_touch = (indev != encoder_indev());
  if (is_touch) { s->onEnterReady(); }
  else          { s->onEnterTouchCalib(); }
}
static void _touchtest_bg_cb(lv_event_t* ev){
  auto s = (TempRegulator*)lv_event_get_user_data(ev);
  s->onEnterTouchCalib();
}

/* Sensor / SSR */
uint16_t TempRegulator::readAdcFiltered(uint8_t& out_outliers) {
  std::vector<uint16_t> v; v.reserve(ADC_READ_SAMPLES);
  for(uint8_t i=0;i<ADC_READ_SAMPLES;i++){ v.push_back((uint16_t)analogRead(THERMOCOUPLE_PIN)); delay(2); }
  auto sv=v; std::nth_element(sv.begin(), sv.begin()+sv.size()/2, sv.end());
  uint16_t med=sv[sv.size()/2]; uint32_t acc=0; uint16_t n=0; out_outliers=0;
  for(auto s:v){ if(abs((int)s-(int)med)<=ADC_OUTLIER_THRESHOLD){acc+=s; n++;} else out_outliers++; }
  if(n==0) return med; return (uint16_t)(acc/n);
}
float TempRegulator::readTemperatureC() {
  uint8_t o=0; uint16_t adc=readAdcFiltered(o);
  if (o > ADC_OUTLIER_ALARM_COUNT) {
    consecutive_outlier_cycles++;
    if (consecutive_outlier_cycles >= 3 && !alarm_active) {
      alarm_active = true;
      onEnterAlarm("Неисправность термопары");
    }
  } else {
    consecutive_outlier_cycles = 0;
  }
  return offset + slope*(float)adc;
}

void TempRegulator::ssrApply() {
  uint32_t now=millis();
  if(now - ssr_window_start >= SSR_WINDOW_MS) { ssr_window_start = now; }
  uint32_t on_time = (uint32_t)((ssr_power_0_255 * SSR_WINDOW_MS)/255);
  bool on = ((now-ssr_window_start) < on_time);
  digitalWrite(SSR_CONTROL_PIN, on ? HIGH : LOW);
}

/* ===== Persistent storage (LittleFS) ===== */
void TempRegulator::saveNVS() {
  PersistentConfig cfg{};
  cfg.calibrated        = isCalibrated;
  cfg.offset            = offset;
  cfg.slope             = slope;
  cfg.pid_kp            = pid_kp;
  cfg.pid_ki            = pid_ki;
  cfg.pid_kd            = pid_kd;
  cfg.touch_calibrated  = g_touch_calibrated;
  cfg.touch_swap        = g_touch_swap_axes;
  cfg.touch_tx_min      = g_tx_min;
  cfg.touch_tx_max      = g_tx_max;
  cfg.touch_ty_min      = g_ty_min;
  cfg.touch_ty_max      = g_ty_max;

  if (!Storage::save(cfg)) {
    Serial.println("[Storage] Failed to save config to LittleFS");
  }
}

bool TempRegulator::loadNVS() {
  PersistentConfig cfg{};
  if (!Storage::load(cfg)) {
    isCalibrated       = false;
    offset             = 0.0f;
    slope              = 1.0f;
    pid_kp             = 2.0;
    pid_ki             = 5.0;
    pid_kd             = 1.0;
    resetTouchCalibrationToDefaults();
    return false;
  }

  isCalibrated       = cfg.calibrated;
  offset             = cfg.offset;
  slope              = cfg.slope;
  pid_kp             = cfg.pid_kp;
  pid_ki             = cfg.pid_ki;
  pid_kd             = cfg.pid_kd;

  g_touch_calibrated = cfg.touch_calibrated;
  g_touch_swap_axes  = cfg.touch_swap;
  g_tx_min           = cfg.touch_tx_min;
  g_tx_max           = cfg.touch_tx_max;
  g_ty_min           = cfg.touch_ty_min;
  g_ty_max           = cfg.touch_ty_max;

  return true;
}

/* ===== Калибровка термопары: логика ===== */
void TempRegulator::updateCalibStableUI(bool st){
  if(!btn_ok) return;
  lv_color_t c = st? lv_color_hex(0x2ecc71) : lv_color_hex(0xe74c3c);
  lv_obj_set_style_bg_color(btn_ok,c,0);
}
void TempRegulator::saveCalibration(float off, float sl){
  offset = off; slope = sl; isCalibrated = true; saveNVS();
}
void TempRegulator::startCalibration(){
  cst = CAL_STEP1_INPUT_AMBIENT; stable = false; stable_t0 = 0;
  cal_t0 = millis();
  createCalibS1();
}
void TempRegulator::tickCalibration(){
  static uint16_t last_adc=0;

  switch(cst){
    case CAL_STEP1_INPUT_AMBIENT:
      if (isCalibTimedOut()) { createCalibMsg("Тайм-аут шага 1"); cst=CAL_ERROR; }
      break;

    case CAL_STEP1_MEASURE: {
      uint8_t o=0; uint16_t a=readAdcFiltered(o);
      if(o>CAL_MAX_OUTLIERS){ createCalibMsg("Неисправность термопары"); cst=CAL_ERROR; break; }
      cal_adc1 = a;
      cal_temp2=100.0f;
      stable=false; stable_t0=0; last_adc=a; cal_t0=millis();
      cst=CAL_STEP2_WAIT_STABLE; createCalibS2();
      break;
    }

    case CAL_STEP2_WAIT_STABLE: {
      uint8_t o=0; uint16_t a=readAdcFiltered(o);
      if(lbl_cal_val){ char b[32]; snprintf(b,sizeof(b),"АЦП: %u",(unsigned)a); lv_label_set_text(lbl_cal_val,b); }
      if(o>CAL_MAX_OUTLIERS){ createCalibMsg("Неисправность термопары"); cst=CAL_ERROR; break; }

      if(abs((int)a-(int)last_adc)<=CAL_STABLE_DELTA){
        if(!stable_t0) stable_t0=millis();
        if(millis()-stable_t0>=CAL_STABLE_HOLD_MS){ stable=true; updateCalibStableUI(true); }
      } else { stable=false; stable_t0=0; updateCalibStableUI(false); }
      last_adc=a;

      if(isCalibTimedOut()){ createCalibMsg("Тайм-аут шага 2"); cst=CAL_ERROR; }
      break;
    }

    case CAL_STEP2_MEASURE: {
      uint8_t o=0; uint16_t a=readAdcFiltered(o);
      if(o>CAL_MAX_OUTLIERS){ createCalibMsg("Неисправность термопары"); cst=CAL_ERROR; break; }
      cal_adc2 = a;
      cst=CALC_COEFFS;
      /* fallthrough */
    }

    case CALC_COEFFS: {
      if(cal_adc2<=cal_adc1 || (cal_adc2-cal_adc1)<CAL_MIN_ADC_DIFF){
        createCalibMsg("Недостаточная разница АЦП"); cst=CAL_ERROR; break;
      }
      float sl=(cal_temp2-cal_temp1)/float(cal_adc2-cal_adc1);
      float off=cal_temp1 - sl*float(cal_adc1);
      saveCalibration(off,sl);
      char m[96]; snprintf(m,sizeof(m),"Готово!\nКоэфф.=%.6f\nСмещение=%.2f",sl,off);
      createCalibMsg(m); cst=CAL_DONE; beep(100);
      break;
    }

    default: break;
  }
}

/* ===== Автонастройка ===== */
void TempRegulator::startAutotune(){
  atst=AT_RUNNING; at_t0=millis(); createAtRun();
  peak_ts.clear(); peak_val.clear();
  float pv = readTemperatureC(); was_above = (pv>at_target);
  relay_on = true; ssr_power_0_255 = 255; ssr_window_start=millis();
}
void TempRegulator::finishAutotune(double kp,double ki,double kd){
  relay_on = false;
  stopHeat();
  pid_kp=kp; pid_ki=ki; pid_kd=kd; saveNVS(); beep(120);
  msgbox("Автонастройка завершена");
  atst=AT_DONE;
}
void TempRegulator::tickAutotune(){
  switch(atst){
    case AT_RUNNING: {
      uint32_t now=millis();
      float t=readTemperatureC();
      if(lbl_at_cur){ char b[32]; snprintf(b,sizeof(b),"T: %.1f °C",t); lv_label_set_text(lbl_at_cur,b); }
      if(lbl_at_time){ char b[24]; snprintf(b,sizeof(b),"t: %lus",(unsigned)((now-at_t0)/1000)); lv_label_set_text(lbl_at_time,b); }

      float pv = t;
      if(relay_on && pv > (at_target + relay_hyst)) { relay_on=false; ssr_power_0_255=0; }
      else if(!relay_on && pv < (at_target - relay_hyst)) { relay_on=true; ssr_power_0_255=255; }

      bool above = pv>at_target;
      if(above!=was_above){
        peak_ts.push_back(now);
        peak_val.push_back(pv);
        was_above=above;
      }

      if(peak_ts.size()>=6){
        double sum=0; int cnt=0;
        for(size_t i=2;i<peak_ts.size();i++) { sum += (double)(peak_ts[i]-peak_ts[i-2]); cnt++; }
        double Tu_ms = sum / (double)cnt;
        double Tu = Tu_ms/1000.0;
        double vmax=*std::max_element(peak_val.begin(), peak_val.end());
        double vmin=*std::min_element(peak_val.begin(), peak_val.end());
        double a = (vmax - vmin)/2.0; if(a<0.1) a=0.1;
        double d = 1.0;
        double Ku = (4.0*d) / (3.1415926535*a);
        double Kp = 0.6 * Ku;
        double Ti = 0.5 * Tu;
        double Td = 0.125 * Tu;
        double Ki = Kp / Ti;
        double Kd = Kp * Td;
        finishAutotune(Kp,Ki,Kd);
      }

      if((now-at_t0)>AT_TIMEOUT_MS){
        atst=AT_ERROR;
        relay_on = false;
        stopHeat();
        msgbox("Тайм-аут автонастройки");
      }
      if(atst==AT_ABORT){
        relay_on = false;
        stopHeat();
        atst=AT_IDLE;
        onEnterSettings();
      }
      break;
    }
    case AT_DONE:  { stopHeat(); onEnterSettings(); atst=AT_IDLE; break; }
    case AT_ERROR: { stopHeat(); onEnterSettings(); atst=AT_IDLE; break; }
    default: break;
  }
}

/* ===== State enter ===== */
void TempRegulator::onEnterReady(){
  clear_encoder_group();
  btn_work_heat = nullptr;
  btn_manual_heat = nullptr;
  lbl_work_cur = nullptr;
  lbl_work_sp = nullptr;
  lbl_work_pow = nullptr;
  lbl_man_cur = nullptr;
  lbl_man_sp = nullptr;
  state = STATE_READY;
  createMain();
}
void TempRegulator::onEnterSettings(){ state = STATE_SETTINGS; createSettings(); }
void TempRegulator::onEnterWork(){
  float desiredTarget = targetC;
  pid.setCoeffs(pid_kp,pid_ki,pid_kd);

  if (activeProfileIndex >= 0 && activeProfileIndex < static_cast<int8_t>(kTemperatureProfileCount)) {
    if (profiles[activeProfileIndex].isAvailable()) {
      const auto& profile = profiles[activeProfileIndex];
      if (profile.hasPidCoefficients()) {
        pid.setCoeffs(profile.kp(), profile.ki(), profile.kd());
      }
      if (profile.stepCount() > 0) {
        desiredTarget = profile.step(0).rEndTemperature;
      }
    }
  }

  setTargetC(desiredTarget);
  ssr_window_start = millis();
  ssr_power_0_255 = 0;
  digitalWrite(SSR_CONTROL_PIN, LOW);
  heating = false;          // нагрев запускается вручную кнопкой «Пуск»
  createWork();
}

void TempRegulator::onEnterManual(){
  state = STATE_MANUAL;
  pid.setCoeffs(pid_kp,pid_ki,pid_kd);
  setTargetC(targetC);
  ssr_window_start = millis();
  ssr_power_0_255 = 0;
  digitalWrite(SSR_CONTROL_PIN, LOW);
  heating = false;          // пользователю нужно включить нагрев вручную
  createManual();
}

void TempRegulator::onEnterCalib(){ startCalibration(); }
void TempRegulator::onEnterAutotune(){ atst=AT_SETUP_TARGET; createAtSetup(); }
void TempRegulator::onEnterAlarm(const char* text){
  lv_obj_t* m = lv_msgbox_create(NULL);
  lv_msgbox_add_text(m, text);
  lv_obj_center(m);
  lv_obj_t* ok = lv_msgbox_add_footer_button(m, "ОК");
  lv_obj_add_event_cb(ok, _alarm_ok_cb, LV_EVENT_CLICKED, this);
  encoder_modal_take({ok});
}
void TempRegulator::onEnterTouchCalib(){ tcs = TCS_1; tcal_idx=0; t_pressed=false; t_press_t0=0; createTouchCalib(); state = STATE_TOUCH_CALIBRATE; }
void TempRegulator::onEnterTouchTest(){ createTouchTest(); state = STATE_TOUCH_TEST; }

/* ===== Touch calib tick ===== */
void TempRegulator::tickTouchCalib() {
  uint16_t rx, ry;
  bool pressed = tft.getTouchRaw(&rx, &ry);

  switch (tcs) {
    case TCS_1:
    case TCS_2:
    case TCS_3:
    case TCS_4: {
      if (pressed) {
        if (!t_pressed) {
          t_pressed = true;
          t_press_t0 = millis();
        }
        if (millis() - t_press_t0 >= 150) {
          rawx[tcal_idx] = rx;
          rawy[tcal_idx] = ry;
          t_pressed = false;
          tcal_idx++;

          if (tcal_idx >= 4) {
            tcs = TCS_DONE;
          } else {
            tcs = static_cast<TouchCalStep>((int)TCS_1 + tcal_idx);
            tcal_next_target();
          }
        }
      } else {
        t_pressed = false;
      }
      break;
    }

    case TCS_DONE:
      tcal_finish_and_save();
      break;

    default:
      break;
  }
}

/* ===== Lifecycle ===== */
void TempRegulator::begin() {
  tft.init();
  tft.setRotation(3);  // Поворачиваем изображение на 180° для ландшафтной ориентации

  lv_init();
  createLvglDisplay();
  initPointerInput();
  EncoderInput::createInputDevice();

  const esp_timer_create_args_t lvgl_tick_args = { .callback=&lv_tick_task, .arg=nullptr, .dispatch_method=ESP_TIMER_TASK, .name="lvgl_tick" };
  esp_timer_handle_t h; ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_args, &h));
  ESP_ERROR_CHECK(esp_timer_start_periodic(h, 5000));

  EncoderInput::setupHardware();

  pinMode(SSR_CONTROL_PIN, OUTPUT); digitalWrite(SSR_CONTROL_PIN, LOW);
  pinMode(SSR_FEEDBACK_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, HIGH);  // Активная логика «низкий уровень»
  pinMode(LED_R_PIN, OUTPUT); pinMode(LED_G_PIN, OUTPUT); pinMode(LED_B_PIN, OUTPUT);
  digitalWrite(LED_R_PIN, HIGH); digitalWrite(LED_G_PIN, HIGH); digitalWrite(LED_B_PIN, HIGH);

  analogReadResolution(12);

  if (!Storage::begin()) {
    Serial.println("[Storage] Failed to mount LittleFS");
  }

  if (!loadNVS()) {
    saveNVS();
  }

  ensureDefaultTemperatureProfiles();
  loadTemperatureProfiles();

  pid.setCoeffs(pid_kp, pid_ki, pid_kd);

  state = STATE_READY;                                                    // Modified: стартуем напрямую без заставки
  onEnterReady();                                                         // Modified: сразу создаём главный экран
}

void TempRegulator::update() {
  lv_timer_handler();

  if (ev != EVENT_NONE) {
    switch (state) {
      case STATE_INIT:
        state = (ev == EVENT_INIT_OK) ? STATE_READY : STATE_ALARM;
        if (state == STATE_READY) onEnterReady();
        else onEnterAlarm("Ошибка инициализации");
        break;

      case STATE_READY:
        if (ev == EVENT_TO_SETTINGS)        { state = STATE_SETTINGS;        onEnterSettings(); }
        else if (ev == EVENT_TO_PROFILES)   {                                 createProfiles(); }
        else if (ev == EVENT_TO_PROFILE_WORK){ state = STATE_WORK;            onEnterWork();    }
        else if (ev == EVENT_TO_MANUAL)     { state = STATE_MANUAL;           onEnterManual();  }
        break;

      case STATE_SETTINGS:
        if (ev == EVENT_TO_CALIB)   { state = STATE_CALIBRATE_SENSOR; onEnterCalib();    }
        else if (ev == EVENT_TO_AUTOTUNE){ state = STATE_AUTOTUNE_PID;    onEnterAutotune(); }
        break;

      case STATE_WORK:
        if (ev == EVENT_STOP) {
          stopHeat();
          state = STATE_READY;
          onEnterReady();
        }
        break;

      case STATE_MANUAL:
        if (ev == EVENT_STOP) {
          stopHeat();
          state = STATE_READY;
          onEnterReady();
        }
        break;

      case STATE_TOUCH_TEST:
      case STATE_TOUCH_CALIBRATE:
        // события не обрабатываем здесь
        break;

      default:
        break;
    }
    ev = EVENT_NONE;
  }

  if (state == STATE_WORK) {
    float pv = readTemperatureC();
    lastTemperatureC = pv;                                                // Modified: сохраняем температуру для веба

    if (heating) {
      ssr_power_0_255 = pid.compute(pv);
    } else {
      ssr_power_0_255 = 0;
      digitalWrite(SSR_CONTROL_PIN, LOW);
    }
    ssrApply();

    if (lbl_work_cur) { char b1[24]; snprintf(b1, sizeof(b1), "%.1f", pv);          lv_label_set_text(lbl_work_cur, b1); }
    if (lbl_work_sp)  { char b2[24]; snprintf(b2, sizeof(b2), "%.1f", targetC);     lv_label_set_text(lbl_work_sp,  b2); }
    if (lbl_work_pow) { char b3[24]; snprintf(b3, sizeof(b3), "%.1f", (double)ssr_power_0_255 * 100.0 / 255.0);
                        lv_label_set_text(lbl_work_pow, b3); }

  } else if (state == STATE_CALIBRATE_SENSOR) {
    tickCalibration();

  } else if (state == STATE_AUTOTUNE_PID) {
    tickAutotune();
    ssrApply();

  } else if (state == STATE_TOUCH_CALIBRATE) {
    tickTouchCalib();

  } else if (state == STATE_MANUAL) {
    float pv = readTemperatureC();
    lastTemperatureC = pv;                                                // Modified: сохраняем температуру для веба

    if (heating) {
      ssr_power_0_255 = pid.compute(pv);
    } else {
      ssr_power_0_255 = 0;
      digitalWrite(SSR_CONTROL_PIN, LOW);
    }
    ssrApply();

    if (lbl_man_cur) { char b1[24]; snprintf(b1, sizeof(b1), "%.1f", pv);          lv_label_set_text(lbl_man_cur, b1); }
    if (lbl_man_sp)  { char b2[24]; snprintf(b2, sizeof(b2), "%.1f", getTargetC()); lv_label_set_text(lbl_man_sp,  b2); }
  }
  WebInterface::instance().updateTelemetry(*this);                        // Modified: сообщаем веб-интерфейсу обновления
}

String TempRegulator::describeStateForWeb() const {                        // Modified: возвращаем строку состояния
  switch (state) {                                                         // Modified: сопоставляем состояния автомата
    case STATE_INIT: return "Инициализация";                               // Modified: стадия запуска
    case STATE_READY: return "Готов";                                      // Modified: основной экран
    case STATE_WORK: return "Работа";                                     // Modified: выполнение профиля
    case STATE_SETTINGS: return "Настройки";                              // Modified: меню настроек
    case STATE_CALIBRATE_SENSOR: return "Калибровка";                     // Modified: процедура калибровки
    case STATE_AUTOTUNE_PID: return "Автонастройка";                      // Modified: автонастройка PID
    case STATE_ALARM: return "Авария";                                    // Modified: аварийное состояние
    case STATE_TOUCH_CALIBRATE: return "Калибровка тача";                 // Modified: калибровка сенсора
    case STATE_TOUCH_TEST: return "Тест тача";                            // Modified: тест сенсорного экрана
    case STATE_MANUAL: return "Ручной режим";                             // Modified: ручное управление
    default: return "Неизвестно";                                        // Modified: значение по умолчанию
  }
}



void TempRegulator::loadTemperatureProfiles() {
  for (size_t i = 0; i < kTemperatureProfileCount; ++i) {
    profiles[i].setNamespace(kProfileNamespaces[i]);
    profiles[i].setDefaultName(kProfileDefaultNames[i]);
    profiles[i].loadFromNVS();
  }

  if (activeProfileIndex >= 0 && activeProfileIndex < static_cast<int8_t>(kTemperatureProfileCount)) {
    if (!profiles[activeProfileIndex].isAvailable()) {
      activeProfileIndex = -1;
    }
  }
}

void TempRegulator::handleProfileSelection(lv_obj_t* target) {
  for (size_t i = 0; i < profileButtonCount; ++i) {
    if (profileButtons[i] == target) {
      activeProfileIndex = static_cast<int8_t>(profileButtonToIndex[i]);
      ev = EVENT_TO_PROFILE_WORK;
      return;
    }
  }
}

/* ===== RESET действия (public интерфейсы) ===== */
void TempRegulator::do_reset_touch(){
  resetTouchCalibrationToDefaults();
  saveNVS();

  onEnterTouchCalib();
}
void TempRegulator::do_reset_tc(){
  isCalibrated = false; offset = 0.0f; slope  = 1.0f;
  saveNVS();

  onEnterSettings();
}
void TempRegulator::do_reset_pid(){
  pid_kp = 2.0; pid_ki = 5.0; pid_kd = 1.0;
  saveNVS();
  if (state == STATE_WORK || state == STATE_MANUAL) pid.setCoeffs(pid_kp, pid_ki, pid_kd);

  onEnterSettings();
}

/* ===== async reset (private) ===== */
void TempRegulator::cb_reset_touch(void* user) { ((TempRegulator*)user)->do_reset_touch(); }
void TempRegulator::cb_reset_tc(void* user)    { ((TempRegulator*)user)->do_reset_tc(); }
void TempRegulator::cb_reset_pid(void* user)   { ((TempRegulator*)user)->do_reset_pid(); }

/* ===== Полный wipe и перезапуск ===== */
void TempRegulator::clearNVS() {
  if (!Storage::clear()) {
    Serial.println("[Storage] Failed to clear config file");
  }

  isCalibrated = false; offset = 0.0f; slope = 1.0f;
  pid_kp = 2.0; pid_ki = 5.0; pid_kd = 1.0;

  resetTouchCalibrationToDefaults();

  saveNVS();

  msgbox("Настройки очищены.\nПерезапуск...");
  beep(80);
  delay(50);
  Serial.flush();
  esp_restart();
  ESP.restart();
  while (true) { delay(1000); }
}

