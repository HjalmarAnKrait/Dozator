#include "ui.h"
#include "link.h"
#include "power.h"
#include "remote_config.h"
#include "ota.h"
#include "version.h"
#include <U8g2lib.h>
#include <qrcode.h>
#include <Wire.h>
#include <Preferences.h>
#include <stdarg.h>
#include <math.h>

namespace ui {

// ════════════════════════ Экран ════════════════════════
// 0.96" SSD1306 128x64, I2C. Сетка: шапка 16 px (на двухцветных модулях это
// жёлтая полоса) + четыре строки по 12 px шрифтом 6x12.
// Пины I2C библиотеке не передаём: она делает на них pinMode и отрывает их от
// аппаратного I2C ESP32 — begin() потом висит на таймаутах. Шину поднимает begin() сам.
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
static const uint8_t* FONT_SMALL = u8g2_font_5x7_t_cyrillic;   // мелочь в шапке (USB)
static const uint8_t* FONT_ROW   = u8g2_font_6x12_t_cyrillic;  // шапка и строки меню
static const uint8_t* FONT_BIG   = u8g2_font_logisoso24_tn;    // цифры 24 px
static constexpr int HEADER_H = 16;
static constexpr int HEADER_BASE = 12;
static constexpr int ROW_H    = 12;
static constexpr int ROWS     = 4;
static constexpr int ROW_BASE = 10;                             // базовая линия текста внутри строки
static bool s_rowFont = false;                                  // сейчас выбран FONT_ROW
static void useSmall() { u8g2.setFont(FONT_SMALL); s_rowFont = false; }
static void useRow()   { u8g2.setFont(FONT_ROW);   s_rowFont = true; }
static int  rowTop(int i) { return HEADER_H + i * ROW_H; }

#define S (rlink::state())

static float spc() {                       // шагов мотора на 1 см
    float p = S.screwPitch > 0.1f ? S.screwPitch : 8.0f;
    return 3200.0f / p * 10.0f;
}
static float toCm(int32_t steps) { return steps / spc(); }

static bool movingScreen(uint8_t sc) {
    return sc == RPS_PARKING || sc == RPS_CHARGING || sc == RPS_DOSING || sc == RPS_CALIBRATING;
}
static bool moving() { return movingScreen(S.screen) || (S.flags & RP_FLAG_MOTOR_BUSY); }

// ── текст: '@' рисуется как значок диаметра (в шрифте его нет) ──
static int textW(const char* s) {
    int w = 0; char buf[48];
    while (*s) {
        const char* at = strchr(s, '@');
        size_t n = at ? (size_t)(at - s) : strlen(s);
        if (n) { n = min(n, sizeof(buf) - 1); memcpy(buf, s, n); buf[n] = 0; w += u8g2.getUTF8Width(buf); }
        if (!at) break;
        w += s_rowFont ? 8 : 6; s = at + 1;
    }
    return w;
}
static int textDraw(int x, int y, const char* s) {
    char buf[48];
    while (*s) {
        const char* at = strchr(s, '@');
        size_t n = at ? (size_t)(at - s) : strlen(s);
        if (n) { n = min(n, sizeof(buf) - 1); memcpy(buf, s, n); buf[n] = 0; x += u8g2.drawUTF8(x, y, buf); }
        if (!at) break;
        if (s_rowFont) { u8g2.drawCircle(x + 3, y - 4, 3); u8g2.drawLine(x, y, x + 6, y - 8); x += 8; }
        else           { u8g2.drawCircle(x + 2, y - 3, 2); u8g2.drawLine(x, y, x + 4, y - 6); x += 6; }
        s = at + 1;
    }
    return x;
}
// Обрезать строку по ширине (по символам UTF-8, не байтам).
static void fit(char* s, int maxW) {
    while (*s && textW(s) > maxW) {
        size_t L = strlen(s);
        do { L--; } while (L > 0 && ((uint8_t)s[L] & 0xC0) == 0x80);
        s[L] = 0;
    }
}

// ════════════════════════ Меню ════════════════════════
enum Act : uint8_t { AC_NONE, AC_BACK, AC_CMD, AC_CMD_CAL, AC_MENU, AC_EDIT, AC_CONFIRM, AC_PICK_PRESET };
enum MenuId : uint8_t { MN_SETTINGS, MN_PRESETS, MN_PRESET, MN_SYRINGE, MN_SYR_PRESET, MN_DEBUG, MN_REMOTE };
enum EditId : uint8_t { ED_DOSE_TIME, ED_DIAM, ED_PITCH, ED_PARK_SPEED, ED_CHARGE_SPEED, ED_H,
                        ED_JOG_STEPS, ED_JOG_SPEED, ED_SLEEP, ED_PRESET_VOL, ED_PRESET_DIAM, ED_BRIGHT };
enum ConfirmId : uint8_t { CF_CALIB, CF_ABORT, CF_CANCEL, CF_DEBUG_EXIT, CF_H_ZERO, CF_OTA, CF_RESET };

struct Item { char label[32]; char value[24]; uint8_t act; uint8_t a1; int8_t a2; };
struct Page { char title[32]; char info[2][48]; uint8_t nInfo; Item items[12]; uint8_t n; };
static Page P;

enum VType : uint8_t { V_HOME, V_MENU, V_EDIT, V_CONFIRM };
struct View {
    uint8_t type, id; int8_t arg; int8_t cursor, scroll;
    float val, vmin, vmax, step; uint8_t dec; uint8_t sel; uint32_t lastRot;
    bool waiting;                 // редактор: значение ушло на дозатор, ждём ACK
};
static View    st[8];
static uint8_t depth = 0;
static View& top() { return st[depth - 1]; }
static void push(const View& v) { if (depth < 8) st[depth++] = v; }
static void pop() { if (depth > 1) depth--; }
static void resetHome() { depth = 1; st[0] = View{}; st[0].type = V_HOME; }
static View menuView(uint8_t id, int8_t arg = 0, int8_t cursor = 0) {
    View v{}; v.type = V_MENU; v.id = id; v.arg = arg; v.cursor = cursor; return v;
}

// ── состояние пульта ──
static uint16_t s_sleepSec = SCREEN_SLEEP_DEFAULT_S;
static bool     s_screenOff = false;
static uint32_t s_lastInput = 0, s_wakeAt = 0, s_lastFrame = 0;
static uint8_t  s_lastScreen = 0xFF;      // экран дозатора, который сейчас нарисован
static uint32_t s_screenChangedMs = 0;
static bool     s_saveWait = false;       // ждём ACK на сохранённое значение — тогда «СОХРАНЕНО»
static bool     s_otaRequest = false;     // комбинация сработала — main.cpp переключит в режим обновления
static char     s_toast[48] = "";
static bool     s_toastOn = false;        // флаг, а не только время: старый тост не «оживёт» через 25 суток
static uint32_t s_toastUntil = 0;
static uint32_t s_guardUntil = 0;         // до этого момента клик и поворот не исполняются
static bool     s_wasOnline = false;
static bool     s_fromDosing = false;     // экран STOPPED наступил прямо из дозирования — показать выданное
static Preferences s_prefs;
static uint32_t s_stopPendingUntil = 0;   // СТОП нажат без связи — ждём связь
static uint32_t s_lastLinkMs = 0;         // когда последний раз была связь — для глубокого сна
static uint8_t  s_bright = BRIGHT_DEFAULT;
static uint8_t  s_brightApplied = 0;
static bool     s_oled = false;           // экран ответил на I2C
static uint32_t s_oledProbeMs = 0;

// ── яркость ──
// Контраст SSD1306 даже на нуле светит заметно. Для работы в темноте на нижних
// уровнях дополнительно укорачиваем предзаряд пикселя (0xD9) и опускаем VCOMH (0xDB):
// так экран тускнеет в разы сильнее, чем от одного контраста.
static void applyBright(uint8_t lvl) {
    struct L { uint8_t contrast, precharge, vcomh; };
    static const L T[10] = {
        // Предзаряд 0x11 на части модулей гасит экран полностью — не ниже 0x22.
        {1,   0x22, 0x00}, {10,  0x22, 0x00}, {20,  0x22, 0x00}, {40,  0xF1, 0x20}, {60,  0xF1, 0x20},
        {85,  0xF1, 0x40}, {115, 0xF1, 0x40}, {150, 0xF1, 0x40}, {200, 0xF1, 0x40}, {255, 0xF1, 0x40},
    };
    const L& l = T[constrain(lvl, 1, 10) - 1];
    u8g2.setContrast(l.contrast);
    u8g2.sendF("ca", 0xD9, l.precharge);
    u8g2.sendF("ca", 0xDB, l.vcomh);
    s_brightApplied = lvl;
}

void showMessage(const char* text, uint32_t ms) {
    strlcpy(s_toast, text, sizeof(s_toast));
    s_toastUntil = millis() + ms;           // сравнивается через разность — переживает переполнение millis
    s_toastOn = true;
}
static void guard(uint32_t now, uint32_t ms) {
    if ((int32_t)(now + ms - s_guardUntil) > 0) s_guardUntil = now + ms;
}
static bool guarded(uint32_t now) { return (int32_t)(s_guardUntil - now) > 0; }

// Виды, которым дозатор не нужен: меню «ПУЛЬТ», его настройки и вход в обновление.
// Они работают и без связи.
static bool isLocal(const View& v) {
    return (v.type == V_MENU && v.id == MN_REMOTE) ||
           (v.type == V_EDIT && (v.id == ED_SLEEP || v.id == ED_BRIGHT)) ||
           (v.type == V_CONFIRM && (v.id == CF_OTA || v.id == CF_RESET));
}

static void pageReset(const char* title) { strlcpy(P.title, title, sizeof(P.title)); P.nInfo = 0; P.n = 0; }
static void info(const char* fmt, ...) {
    if (P.nInfo >= 2) return;
    va_list ap; va_start(ap, fmt); vsnprintf(P.info[P.nInfo++], sizeof(P.info[0]), fmt, ap); va_end(ap);
}
static Item& add(const char* label, uint8_t act, uint8_t a1 = 0, int8_t a2 = 0) {
    static Item dummy;
    if (P.n >= 12) return dummy;
    Item& it = P.items[P.n++];
    strlcpy(it.label, label, sizeof(it.label));
    it.value[0] = 0; it.act = act; it.a1 = a1; it.a2 = a2;
    return it;
}
static void val(Item& it, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(it.value, sizeof(it.value), fmt, ap); va_end(ap);
}
static void mmss(char* out, size_t n, float sec) {
    int s = (int)max(0.0f, sec);
    snprintf(out, n, "%d:%02d", s / 60, s % 60);
}

static const char* causeText(uint8_t c) {
    switch (c) {
        case RPSC_STUCK_HOME:   return "TOP НЕ НАЙДЕН";
        case RPSC_STUCK_CHARGE: return "A И B НЕ НАЖАЛИСЬ";
        case RPSC_STUCK_END:    return "BOT НЕ НАЙДЕН";
        case RPSC_CALIB_FAIL:   return "БРАК КАЛИБРОВКИ";
        case RPSC_BAD_CHARGE:   return "ЗАРЯД НИЖЕ H";
        default:                return "ВРУЧНУЮ";
    }
}

// Значки в правой колонке: «>» — подменю, «!» — нужна калибровка хода.
static void addCalib(bool cal) { Item& i = add("КАЛИБРОВКА ХОДА", AC_CONFIRM, CF_CALIB); if (!cal) val(i, "!"); }
static void addSub(const char* label, uint8_t menu) { val(add(label, AC_MENU, menu), ">"); }
static void buildHome() {
    char t[16];
    bool cal = S.fullPathSteps > 0;
    switch (S.screen) {
        case RPS_IDLE:
            pageReset("ПРОСТОЙ");
            add("ПАРКОВКА", AC_CMD, RPC_PARK);
            addCalib(cal);
            add("РУЧНОЙ РЕЖИМ", AC_CMD, RPC_DEBUG);
            addSub("НАСТРОЙКИ", MN_SETTINGS);
            addSub("ПУЛЬТ", MN_REMOTE);
            break;
        case RPS_PARKING:
            pageReset("ПАРКОВКА");
            info("ЕДУ ДОМОЙ К TOP");
            info("ПОЗИЦИЯ %.2f СМ", toCm(S.motorPos));
            add("СТОП", AC_CMD, RPC_STOP);
            break;
        case RPS_PARKED:
            pageReset("ГОТОВ");
            { Item& i = add("СТАРТ ЗАРЯДКИ", AC_CMD_CAL, RPC_START_CHARGING); if (!cal) val(i, "!"); }
            for (int s = 0; s < 2; s++) {
                int8_t idx = s ? S.presetB : S.presetA;
                float vol = (idx >= 0 && idx < S.presetsCount) ? S.presets[idx].vol : 0;
                val(add(s ? "ШПРИЦ B" : "ШПРИЦ A", AC_MENU, MN_SYRINGE, s), "%gМЛ @%.1f", vol, s ? S.diamB : S.diamA);
            }
            addCalib(cal);
            add("РУЧНОЙ РЕЖИМ", AC_CMD, RPC_DEBUG);
            addSub("НАСТРОЙКИ", MN_SETTINGS);
            addSub("ПУЛЬТ", MN_REMOTE);
            break;
        case RPS_CHARGING:
            pageReset("ЗАРЯДКА");
            info("ЖДУ УПОРА A И B");                  // сами A/B — в шапке
            info("ПОЗИЦИЯ %.2f СМ", toCm(S.motorPos));
            add("СТОП", AC_CMD, RPC_STOP);
            break;
        case RPS_CHARGED:
            pageReset("ЗАРЯЖЕНО");
            info("ДОЗА A%.1f B%.1f МЛ", S.doseVolA, S.doseVolB);
            mmss(t, sizeof(t), roundf(S.doseTimeMin * 60.0f));
            val(add("ВРЕМЯ ДОЗЫ", AC_EDIT, ED_DOSE_TIME), "%s", t);
            add("ПУСК", AC_CMD_CAL, RPC_PUSK);
            add("ОТМЕНИТЬ ЦИКЛ", AC_CONFIRM, CF_CANCEL);
            addSub("ПУЛЬТ", MN_REMOTE);
            break;
        case RPS_DONE:
            pageReset(S.doneReason == RPD_BOT ? "ГОТОВО: BOT" : S.doneReason == RPD_ABORT ? "ПРЕРВАНО" : "ГОТОВО");
            info("A %.1f  B %.1f МЛ", S.volumeA, S.volumeB);
            add("НОВЫЙ ЦИКЛ", AC_CMD, RPC_NEW_CYCLE);
            add("ПАРКОВКА", AC_CMD, RPC_PARK);
            addCalib(cal);
            add("РУЧНОЙ РЕЖИМ", AC_CMD, RPC_DEBUG);
            addSub("НАСТРОЙКИ", MN_SETTINGS);
            addSub("ПУЛЬТ", MN_REMOTE);
            break;
        case RPS_STOPPED:
            pageReset("ОСТАНОВЛЕНО");
            info("%s", causeText(S.stopCause));
            // Дозу остановили кнопкой СТОП — сколько успело выйти (кликом «ПРЕРВАТЬ»
            // объём пишется в «ПРЕРВАНО», а СТОП ведёт сюда).
            if (s_fromDosing) info("ВЫШЛО A%.1f B%.1f МЛ", S.volumeA, S.volumeB);
            add("ПАРКОВКА", AC_CMD, RPC_PARK);       // калибровку дозатор из STOPPED не принимает
            add("РУЧНОЙ РЕЖИМ", AC_CMD, RPC_DEBUG);
            addSub("НАСТРОЙКИ", MN_SETTINGS);
            addSub("ПУЛЬТ", MN_REMOTE);
            break;
        case RPS_CALIBRATING:
            snprintf(t, sizeof(t), "КАЛИБР. %d/2", S.calibPhase + 1);
            pageReset(t);
            info(S.calibPhase ? "МЕРЯЮ ХОД ДО BOT" : "ИЩУ ДОМ (TOP)");
            info("ПОЗИЦИЯ %.2f СМ", toCm(S.motorPos));
            add("СТОП", AC_CMD, RPC_STOP);
            break;
        default:                                  // состояние, которого этот пульт не знает
            pageReset("?");
            info("НЕИЗВЕСТНОЕ СОСТОЯНИЕ");
            info("СТОП - КНОПКОЙ СТОП");
            addSub("ПУЛЬТ", MN_REMOTE);
            break;
    }
}

static void buildMenu(uint8_t id, int8_t arg) {
    switch (id) {
        case MN_SETTINGS:
            pageReset("НАСТРОЙКИ");
            val(add("ШАГ ВИНТА", AC_EDIT, ED_PITCH), "%.1fММ", S.screwPitch);
            val(add("V ПАРКОВКИ", AC_EDIT, ED_PARK_SPEED), "%.2fСМ/С", toCm(S.parkSpeed));
            val(add("V ЗАРЯДКИ", AC_EDIT, ED_CHARGE_SPEED), "%.2fСМ/С", toCm(S.chargeSpeed));
            { Item& i = add("ХОД H", AC_EDIT, ED_H);
              if (S.fullPathSteps > 0) val(i, "%.2fСМ", toCm(S.fullPathSteps)); else val(i, "!"); }
            addSub("ПРЕСЕТЫ ШПРИЦОВ", MN_PRESETS);
            val(add("РУЧНОЙ ШАГ", AC_EDIT, ED_JOG_STEPS), "%.2fСМ", toCm(S.jogSteps));
            val(add("V РУЧНАЯ", AC_EDIT, ED_JOG_SPEED), "%.2fСМ/С", toCm(S.jogSpeed));
            add("НАЗАД", AC_BACK);
            break;
        case MN_PRESETS:
            pageReset("ПРЕСЕТЫ");
            for (int i = 0; i < S.presetsCount && i < RP_MAX_PRESETS; i++) {
                char l[24]; snprintf(l, sizeof(l), "%d: %gМЛ", i + 1, S.presets[i].vol);
                val(add(l, AC_MENU, MN_PRESET, i), "@%.1f", S.presets[i].diam);
            }
            add("НАЗАД", AC_BACK);
            break;
        case MN_PRESET: {
            char t[20]; snprintf(t, sizeof(t), "ПРЕСЕТ %d", arg + 1);
            pageReset(t);
            val(add("ОБЪЁМ", AC_EDIT, ED_PRESET_VOL, arg), "%gМЛ", S.presets[arg].vol);
            val(add("ДИАМЕТР", AC_EDIT, ED_PRESET_DIAM, arg), "%.1fММ", S.presets[arg].diam);
            add("НАЗАД", AC_BACK);
            break;
        }
        case MN_SYRINGE: {
            pageReset(arg ? "ШПРИЦ B" : "ШПРИЦ A");
            int8_t idx = arg ? S.presetB : S.presetA;
            Item& p = add("ПРЕСЕТ", AC_MENU, MN_SYR_PRESET, arg);
            if (idx >= 0 && idx < S.presetsCount) val(p, "%gМЛ", S.presets[idx].vol); else val(p, "--");
            val(add("ДИАМЕТР", AC_EDIT, ED_DIAM, arg), "%.1fММ", arg ? S.diamB : S.diamA);
            add("НАЗАД", AC_BACK);
            break;
        }
        case MN_SYR_PRESET:
            pageReset(arg ? "ПРЕСЕТ B" : "ПРЕСЕТ A");
            for (int i = 0; i < S.presetsCount && i < RP_MAX_PRESETS; i++) {
                char l[20]; snprintf(l, sizeof(l), "%gМЛ", S.presets[i].vol);
                val(add(l, AC_PICK_PRESET, i, arg), "@%.1f", S.presets[i].diam);
            }
            if (!S.presetsCount) info("НЕТ ПРЕСЕТОВ");
            add("НАЗАД", AC_BACK);
            break;
        case MN_DEBUG:
            pageReset("РУЧНОЙ");
            val(add("РУЧНОЙ ШАГ", AC_EDIT, ED_JOG_STEPS), "%.2fСМ", toCm(S.jogSteps));
            val(add("V РУЧНАЯ", AC_EDIT, ED_JOG_SPEED), "%.2fСМ/С", toCm(S.jogSpeed));
            add("ВЫЙТИ ИЗ РУЧНОГО", AC_CMD, RPC_DEBUG_BACK);
            addSub("ПУЛЬТ", MN_REMOTE);
            add("НАЗАД", AC_BACK);
            break;
        case MN_REMOTE: {
            // Сначала то, что меняется, потом справка (её строки курсор обводит рамкой —
            // нажимать там нечего). Всё это работает и без связи с дозатором.
            pageReset("ПУЛЬТ");
            val(add("ЯРКОСТЬ", AC_EDIT, ED_BRIGHT), "%u/10", s_bright);
            val(add("СОН ЭКРАНА", AC_EDIT, ED_SLEEP), "%uС", s_sleepSec);
            add("ОБНОВИТЬ ПРОШИВКУ", AC_CONFIRM, CF_OTA);
            add("СБРОС НАСТРОЕК", AC_CONFIRM, CF_RESET);
            Item& b = add("БАТАРЕЯ", AC_NONE);
            if (power::hasBattery()) val(b, "%.2fВ %u%%", power::voltage(), power::percent()); else val(b, "USB");
            { Item& s = add("СИГНАЛ", AC_NONE);
              if (rlink::rssi()) val(s, "%dДБМ", rlink::rssi()); else val(s, "--"); }
            val(add("ОТВЕТ", AC_NONE), "%luМС", (unsigned long)min<uint32_t>(rlink::rttMs(), 9999));
            const uint8_t* m = rlink::peerMac();
            Item& d = add("ДОЗАТОР", AC_NONE);
            if (m) val(d, "%02X%02X%02X", m[3], m[4], m[5]); else val(d, "--");
            val(add("КАНАЛ", AC_NONE), "%d", RP_CHANNEL);
            val(add("ВЕРС.", AC_NONE), "%s", kFwVersion);
            { char fw[RP_FW_LEN + 1]; rlink::dozatorFw(fw, sizeof(fw)); val(add("ДОЗ.", AC_NONE), "%s", fw); }
            add("НАЗАД", AC_BACK);
            break;
        }
    }
}

static void buildFor(const View& v) {
    if (v.type == V_HOME) buildHome(); else buildMenu(v.id, v.arg);
}

// ── редактор чисел ──
static const char* editTitleBase(uint8_t id) {
    switch (id) {
        case ED_DOSE_TIME: return "ВРЕМЯ ДОЗЫ";      case ED_DIAM: return "ДИАМЕТР";
        case ED_PITCH: return "ШАГ ВИНТА";           case ED_PARK_SPEED: return "V ПАРКОВКИ";
        case ED_CHARGE_SPEED: return "V ЗАРЯДКИ";    case ED_H: return "ХОД H";
        case ED_JOG_STEPS: return "РУЧНОЙ ШАГ";      case ED_JOG_SPEED: return "V РУЧНАЯ";
        case ED_SLEEP: return "СОН ЭКРАНА";          case ED_PRESET_VOL: return "ОБЪЁМ";
        case ED_PRESET_DIAM: return "ДИАМЕТР";       case ED_BRIGHT: return "ЯРКОСТЬ";
        default: return "";
    }
}
// Заголовок с тем, чьё это значение: «ДИАМЕТР A», «ПРЕСЕТ 3: ОБЪЁМ».
static void editTitle(const View& v, char* out, size_t n) {
    if (v.id == ED_DIAM) snprintf(out, n, "ДИАМЕТР %s", v.arg ? "B" : "A");
    else if (v.id == ED_PRESET_VOL || v.id == ED_PRESET_DIAM)
        snprintf(out, n, "П%d: %s", v.arg + 1, v.id == ED_PRESET_VOL ? "ОБЪЁМ" : "ДИАМЕТР");
    else strlcpy(out, editTitleBase(v.id), n);
}
static const char* editUnit(uint8_t id) {
    switch (id) {
        case ED_DOSE_TIME: return "МИН";
        case ED_DIAM: case ED_PRESET_DIAM: return "ММ";
        case ED_PITCH: return "ММ/ОБ";
        case ED_PARK_SPEED: case ED_CHARGE_SPEED: case ED_JOG_SPEED: return "СМ/С";
        case ED_H: case ED_JOG_STEPS: return "СМ";
        case ED_SLEEP: return "С";
        case ED_PRESET_VOL: return "МЛ";
        case ED_BRIGHT: return "/10";
        default: return "";
    }
}
static void openEdit(uint8_t id, int8_t arg) {
    View v{}; v.type = V_EDIT; v.id = id; v.arg = arg; v.dec = 1; v.step = 0.1f;
    float k = spc();
    switch (id) {
        case ED_DOSE_TIME:                         // в секундах; правится двумя полями: минуты, потом секунды
            v.val = roundf(S.doseTimeMin * 60);
            v.vmin = ceilf(max(0.3f, S.timeMin) * 60); v.vmax = floorf(min(20.0f, S.timeMax) * 60);
            if (v.vmax < v.vmin) v.vmax = v.vmin;
            v.step = 1; v.dec = 0; v.sel = 0;
            break;
        case ED_DIAM:        v.val = arg ? S.diamB : S.diamA; v.vmin = 1; v.vmax = 150; break;
        case ED_PITCH:       v.val = S.screwPitch; v.vmin = 0.5f; v.vmax = 20; break;
        case ED_PARK_SPEED:  v.val = S.parkSpeed / k;   v.vmin = RP_SPEED_MIN / k; v.vmax = RP_SPEED_MAX / k; v.step = 0.01f; v.dec = 2; break;
        case ED_CHARGE_SPEED:v.val = S.chargeSpeed / k; v.vmin = RP_SPEED_MIN / k; v.vmax = RP_SPEED_MAX / k; v.step = 0.01f; v.dec = 2; break;
        case ED_JOG_SPEED:   v.val = S.jogSpeed / k;    v.vmin = RP_SPEED_MIN / k; v.vmax = RP_SPEED_MAX / k; v.step = 0.01f; v.dec = 2; break;
        case ED_H:           v.val = S.fullPathSteps / k; v.vmin = 0; v.vmax = 5000000 / k;   // как в прошивке дозатора v.step = 0.01f; v.dec = 2; break;
        case ED_JOG_STEPS:   v.val = S.jogSteps / k;    v.vmin = 1 / k; v.vmax = 100000 / k; v.step = 0.01f; v.dec = 2; break;
        case ED_SLEEP:       v.val = s_sleepSec; v.vmin = 5; v.vmax = 300; v.step = 5; v.dec = 0; break;
        case ED_BRIGHT:      v.val = s_bright; v.vmin = 1; v.vmax = 10; v.step = 1; v.dec = 0; break;
        case ED_PRESET_VOL:  v.val = S.presets[arg].vol;  v.vmin = 1; v.vmax = 500; v.step = 1; v.dec = 0; break;
        case ED_PRESET_DIAM: v.val = S.presets[arg].diam; v.vmin = 1; v.vmax = 150; break;
    }
    v.val = constrain(v.val, v.vmin, v.vmax);
    push(v);
}
// true — значение ушло на дозатор (подтверждение придёт ACK), false — сохранено в пульте.
static bool saveEdit(const View& v) {
    float k = spc();
    switch (v.id) {
        case ED_DOSE_TIME:    rlink::sendSet(RPF_DOSE_TIME, v.val / 60.0f); break;
        case ED_DIAM:         rlink::sendSet(v.arg ? RPF_B_DIAM : RPF_A_DIAM, v.val); break;
        case ED_PITCH:        rlink::sendSet(RPF_PITCH, v.val); break;
        case ED_PARK_SPEED:   rlink::sendSet(RPF_PARK_SPEED, roundf(v.val * k)); break;
        case ED_CHARGE_SPEED: rlink::sendSet(RPF_CHARGE_SPEED, roundf(v.val * k)); break;
        case ED_JOG_SPEED:    rlink::sendSet(RPF_JOG_SPEED, roundf(v.val * k)); break;
        case ED_H:            rlink::sendSet(RPF_H, roundf(v.val * k)); break;
        case ED_JOG_STEPS:    rlink::sendSet(RPF_JOG_STEPS, max(1.0f, roundf(v.val * k))); break;
        case ED_SLEEP:
            s_sleepSec = (uint16_t)v.val;
            s_prefs.putUShort("sleep", s_sleepSec);
            return false;
        case ED_BRIGHT:
            s_bright = (uint8_t)v.val;
            s_prefs.putUChar("bright", s_bright);
            return false;
        case ED_PRESET_VOL:   rlink::sendPreset(v.arg, v.val, S.presets[v.arg].diam); break;
        case ED_PRESET_DIAM:  rlink::sendPreset(v.arg, S.presets[v.arg].vol, v.val); break;
    }
    return true;
}
static void editHint(const View& v, char* out, size_t n) {
    float k = spc();
    switch (v.id) {
        case ED_DOSE_TIME:
            snprintf(out, n, "A %.1f  B %.1f МЛ/МИН", S.doseVolA * 60 / v.val, S.doseVolB * 60 / v.val); break;
        case ED_DIAM: case ED_PRESET_DIAM:
            snprintf(out, n, "ПЛОЩАДЬ %d ММ2", (int)(3.14159f * v.val * v.val / 4)); break;
        case ED_PITCH:
            snprintf(out, n, "1 СМ = %d ШАГ", (int)(3200.0f / v.val * 10)); break;
        case ED_PARK_SPEED: case ED_CHARGE_SPEED: case ED_JOG_SPEED:
            snprintf(out, n, "= %d ШАГ/С", (int)roundf(v.val * k)); break;
        case ED_H: case ED_JOG_STEPS:
            snprintf(out, n, "= %ld ШАГ", (long)roundf(v.val * k)); break;
        case ED_SLEEP:
            snprintf(out, n, "НАСТРОЙКА ПУЛЬТА"); break;
        case ED_BRIGHT:
            snprintf(out, n, v.val <= 1 ? "МИНИМУМ, ДЛЯ ТЕМНОТЫ" : "1 - ДЛЯ ТЕМНОТЫ"); break;
        default:
            snprintf(out, n, "%g..%g", v.vmin, v.vmax); break;
    }
}

// ── подтверждения ──
static const char* const CF_TEXT[][3] = {
    // Вопрос — до 11 знаков: справа в шапке концевики.
    {"КАЛИБРОВКА?", "СНИМИ ШПРИЦЫ:",      "КАРЕТКА ЕДЕТ ДО BOT"},
    {"ПРЕРВАТЬ?",   "ДОЗА ОСТАНОВИТСЯ,",  "ОБЪЁМ ЗАПИШЕТСЯ"},
    {"ОТМЕНИТЬ?",   "ЗАРЯД СБРОСИТСЯ,",   "КАРЕТКА ОСТАНЕТСЯ"},
    {"ВЫЙТИ?",      "ИЗ РУЧНОГО РЕЖИМА,", "КАРЕТКА ОСТАНЕТСЯ"},
    {"СБРОС H?",    "ХОД H = 0:",         "КАЛИБРОВКА ПОТЕРЯЕТСЯ"},
    {"ОБНОВИТЬ?",   "ПУЛЬТ ОТКЛЮЧИТСЯ",   "ОТ ДОЗАТОРА"},
    {"СБРОС?",      "ЯРКОСТЬ И СОН ЭКРАНА", "ПО УМОЛЧАНИЮ"},
};
// «НЕТ» по умолчанию; первые CONFIRM_GUARD_MS диалог не реагирует.
static void openConfirm(uint8_t id) {
    View v{}; v.type = V_CONFIRM; v.id = id; v.sel = 0; push(v);
    guard(millis(), CONFIRM_GUARD_MS);
}
static void tryOta(uint32_t now);
static void factoryReset();
static void confirmYes(uint8_t id) {
    switch (id) {
        // expect — экран, который был нарисован, когда нажали (а не уже пришедший новый).
        case CF_CALIB:  rlink::sendCmd(RPC_CALIBRATE, s_lastScreen); break;
        case CF_ABORT:  rlink::sendCmd(RPC_ABORT, s_lastScreen); break;
        case CF_CANCEL: rlink::sendCmd(RPC_STOP, s_lastScreen); break;
        case CF_DEBUG_EXIT: rlink::sendCmd(RPC_DEBUG_BACK, s_lastScreen); break;
        case CF_H_ZERO: rlink::sendSet(RPF_H, 0); s_saveWait = true; break;
        case CF_OTA:    tryOta(millis()); break;
        case CF_RESET:  factoryReset(); break;
    }
}

static void doItem(const Item& it) {
    switch (it.act) {
        case AC_BACK:    pop(); break;
        case AC_CMD:     rlink::sendCmd(it.a1, s_lastScreen); break;
        case AC_CMD_CAL:
            if (S.fullPathSteps <= 0) showMessage("НУЖНА КАЛИБРОВКА", TOAST_LONG_MS);
            else rlink::sendCmd(it.a1, s_lastScreen);
            break;
        case AC_MENU: {
            int8_t cur = 0;
            if (it.a1 == MN_SYR_PRESET) cur = max<int8_t>(0, it.a2 ? S.presetB : S.presetA);
            push(menuView(it.a1, it.a2, cur));
            break;
        }
        case AC_EDIT:    openEdit(it.a1, it.a2); break;
        case AC_CONFIRM: openConfirm(it.a1); break;
        case AC_PICK_PRESET:
            rlink::sendSet(it.a2 ? RPF_B_PRESET : RPF_A_PRESET, it.a1);
            s_saveWait = true;                    // «СОХРАНЕНО» — по ACK, как у правки
            pop();
            break;
    }
}

// ════════════════════════ Ввод ════════════════════════
static void wake(uint32_t now) {
    // Проснулись — остаток того же поворота/нажатия не должен ничего сделать.
    if (s_screenOff) { if (s_oled) u8g2.setPowerSave(0); s_wakeAt = now; guard(now, WAKE_GUARD_MS); }
    s_screenOff = false;
    s_lastInput = now;
}

// СТОП мотора. Шлём всегда: что именно останавливать, решает дозатор по своему
// текущему состоянию (RPC_HALT в покое ничего не делает и цикл не отменяет).
static void stopMotor() {
    rlink::sendCmd(RPC_HALT);
    if (moving()) showMessage("СТОП", TOAST_SHORT_MS);   // в покое молчим: возможно, кнопку держат ради парковки
}

// Сброс настроек пульта (яркость, сон экрана) — пунктом меню или удержанием ручки
// при включении. Настройки дозатора не трогает.
static void factoryReset() {
    s_prefs.clear();
    s_sleepSec = SCREEN_SLEEP_DEFAULT_S;
    s_bright = BRIGHT_DEFAULT;
    if (s_oled) applyBright(s_bright);
    resetHome();
    wake(millis());
    showMessage("НАСТРОЙКИ СБРОШЕНЫ", TOAST_LONG_MS);
}

// Вход в обновление — и комбинацией, и пунктом «ПУЛЬТ → ОБНОВИТЬ ПРОШИВКУ».
static void tryOta(uint32_t now) {
    wake(now);
    if (rlink::online(now) && moving()) showMessage("СНАЧАЛА ОСТАНОВИ", TOAST_LONG_MS);
    else if (power::hasBattery() && power::percent() < OTA_MIN_BATTERY_PCT) showMessage("ЗАРЯДИ ПУЛЬТ", TOAST_LONG_MS);
    else s_otaRequest = true;
}

// Правила жестов (одни на всех экранах):
//   поворот        — курсор или значение; в ручном режиме — толчок каретки;
//   клик           — выполнить выделенное / сохранить / ответить в диалоге;
//   удержание ручки — назад без сохранения (в ручном режиме — «ВЫЙТИ?»);
//   СТОП           — остановить мотор, экран не меняется;
//   удержание СТОП — парковка из любого состояния;
//   ручка + СТОП 10 с — обновление прошивки (то же — в меню «ПУЛЬТ»).
// Клик и поворот не исполняются в защитное окно (см. guard()): после смены состояния
// дозатора, остановки мотора, пробуждения экрана, возврата связи, открытия диалога.
void handle(const input::Events& e, uint32_t now) {
    if (!e.any()) return;
    if (e.reset) { factoryReset(); return; }
    if (e.ota) { tryOta(now); return; }
    // Удержание СТОП — парковка из любого состояния (дозатор сам остановит мотор).
    // Без связи не копим: ехать куда-то по старой команде опаснее, чем не поехать.
    if (e.park) {
        wake(now);
        if (rlink::online(now)) { rlink::sendCmd(RPC_HOME); showMessage("ПАРКОВКА", TOAST_SHORT_MS); }   // RPS_ANY
        else showMessage("НЕТ СВЯЗИ", TOAST_SHORT_MS);
        return;
    }
    // Кнопка СТОП работает и на погасшем экране, и без связи: команда уйдёт,
    // как только пульт снова услышит дозатор (см. tick()). Исключение — СТОП к уже
    // зажатой ручке, пока мотор стоит: это начало комбинации обновления, а не остановка.
    if (e.stop) {
        wake(now);
        bool on = rlink::online(now);
        if (e.stopWithEnc && !(on && moving())) return;
        if (on) stopMotor();
        else s_stopPendingUntil = now + STOP_PENDING_MS;
        return;
    }
    if (s_screenOff) { wake(now); return; }      // первое касание только будит экран
    s_lastInput = now;

    View& v = top();
    bool on = rlink::online(now);
    // Без связи работает только своё меню пульта; клик на экране «НЕТ СВЯЗИ» его открывает.
    if (!on && !isLocal(v)) {
        if (e.click && !guarded(now)) push(menuView(MN_REMOTE));
        return;
    }
    // Дозатор только что сменил состояние (или новое ещё не нарисовано): клик и
    // поворот целились в пункт старого экрана — на новом там может быть «СТАРТ»
    // вместо «СТОП». Не исполняем. Мотор останавливает кнопка СТОП, она не ждёт.
    if ((e.click || e.detents) && (guarded(now) || (on && S.screen != s_lastScreen))) return;

    uint8_t sc = S.screen;

    if (e.longPress) {
        if (v.type == V_HOME && sc == RPS_DEBUG) openConfirm(CF_DEBUG_EXIT);
        else if (v.type == V_EDIT && v.id == ED_DOSE_TIME && v.sel == 1 && !v.waiting) v.sel = 0;   // секунды → минуты
        else pop();
        return;
    }

    if (e.detents) {
        if (v.type == V_EDIT) {
            if (v.waiting) return;
            // Ускорение — по темпу вращения; много щелчков за один опрос (кадр
            // экрана держит цикл) — только ×4, чтобы не прыгать на десятки шагов.
            uint32_t gap = now - v.lastRot; v.lastRot = now;
            int abs_d = abs(e.detents);
            float mult = gap < 40 ? 10 : (gap < 100 || abs_d > 2) ? 4 : 1;
            if (v.id == ED_DOSE_TIME) {            // поле минут шагает по минуте, секунд — по секунде
                float x = v.val + e.detents * (v.sel == 0 ? 60 : mult);
                v.val = constrain(roundf(x), v.vmin, v.vmax);
                return;
            }
            float x = v.val + e.detents * v.step * mult;
            x = roundf(x / v.step) * v.step;
            v.val = constrain(x, v.vmin, v.vmax);
        } else if (v.type == V_CONFIRM) {
            v.sel = e.detents > 0 ? 1 : 0;
        } else if (v.type == V_HOME && sc == RPS_DEBUG) {
            rlink::sendJog((int32_t)e.detents * S.jogSteps);
        } else if (v.type == V_HOME && sc == RPS_DOSING) {
            // крутить нечего
        } else {
            buildFor(v);
            v.cursor = constrain(v.cursor + e.detents, 0, max(0, P.n - 1));
        }
    }

    if (e.click) {
        if (v.type == V_EDIT) {
            if (v.waiting) return;
            if (v.id == ED_DOSE_TIME && v.sel == 0) { v.sel = 1; return; }   // минуты → секунды, сохранение — со секунд
            if (v.id == ED_H && v.val <= 0 && S.fullPathSteps > 0) {   // обнулить H = снять калибровку
                pop(); openConfirm(CF_H_ZERO);
            } else if (saveEdit(v)) {
                v.waiting = true;                 // закроется по ACK; при отказе останется открытым
                s_saveWait = true;
            } else {
                pop(); showMessage("СОХРАНЕНО", TOAST_SHORT_MS);
            }
        } else if (v.type == V_CONFIRM) {
            uint8_t id = v.id; bool yes = v.sel == 1; pop();
            if (yes) confirmYes(id);
        } else if (v.type == V_HOME && sc == RPS_DEBUG) {
            push(menuView(MN_DEBUG));
        } else if (v.type == V_HOME && sc == RPS_DOSING) {
            openConfirm(CF_ABORT);
        } else {
            buildFor(v);
            if (v.cursor < P.n) { Item it = P.items[v.cursor]; doItem(it); }
        }
    }
}

// ════════════════════════ Отрисовка ════════════════════════
static void drawIcons(uint32_t now) {
    bool ok = rlink::online(now);
    // Пульс: точка переключается на каждый принятый пакет. Мигает — связь живая.
    if (ok && rlink::heartbeat()) u8g2.drawBox(97, 6, 4, 4);
    else if (!ok)                 u8g2.drawFrame(97, 6, 4, 4);
    // Полоски — уровень сигнала дозатора.
    int8_t r = rlink::rssi();
    int bars = !ok ? 0 : r == 0 ? 3 : r > -65 ? 3 : r > -78 ? 2 : 1;
    for (int i = 0; i < 3; i++) {
        int h = 3 + 2 * i;
        if (i < bars) u8g2.drawBox(104 + i * 3, 12 - h, 2, h); else u8g2.drawPixel(104 + i * 3, 11);
    }
    if (!power::hasBattery()) { useSmall(); u8g2.drawUTF8(114, 11, "USB"); return; }
    uint8_t pct = power::percent();
    if (pct < 10 && (now / 500) % 2) return;     // садится — мигает
    u8g2.drawFrame(114, 4, 12, 8);
    u8g2.drawBox(126, 6, 2, 4);
    u8g2.drawBox(116, 6, (pct * 8 + 50) / 100, 4);
}
// Концевики в шапке, всегда в одном месте: A B L(ow = BOT) T(op). Нажат — буква,
// отпущен — точка. Без связи не рисуем: данные устарели.
static constexpr int SW_X = 70;                   // 4 знака по 6 px до значков связи (97)
static void drawSwitches() {
    static const char    L[4] = {'A', 'B', 'L', 'T'};
    static const uint8_t B[4] = {RP_SW_A, RP_SW_B, RP_SW_BOT, RP_SW_TOP};
    for (int i = 0; i < 4; i++) {
        int x = SW_X + i * 6;
        if (S.switches & B[i]) { char c[2] = {L[i], 0}; u8g2.drawStr(x, HEADER_BASE, c); }
        else u8g2.drawBox(x + 2, HEADER_BASE - 2, 2, 2);
    }
}
static void drawHeader(const char* title, uint32_t now) {
    useRow();
    bool sw = rlink::online(now);
    char t[32]; strlcpy(t, title, sizeof(t));
    fit(t, sw ? SW_X - 3 : 94);
    textDraw(0, HEADER_BASE, t);
    if (sw) drawSwitches();
    drawIcons(now);
    useSmall();
}
// Строки страницы: сначала подсказки (не выбираются), потом пункты. Видно ROWS строк.
// На первом пункте список прокручен в начало — подсказка снова видна.
static void drawList(View& v, uint32_t now) {
    int total = P.nInfo + P.n;
    if (total == 0) return;
    if (P.n) v.cursor = constrain(v.cursor, 0, P.n - 1);
    int curRow = P.nInfo + v.cursor;
    if (v.cursor == 0) v.scroll = 0;
    if (curRow < v.scroll) v.scroll = curRow;
    if (curRow >= v.scroll + ROWS) v.scroll = curRow - ROWS + 1;
    v.scroll = constrain(v.scroll, 0, max(0, total - ROWS));
    bool sb = total > ROWS;
    int wA = sb ? 124 : 128;
    useRow();
    for (int i = 0; i < ROWS; i++) {
        int r = v.scroll + i;
        if (r >= total) break;
        int y = rowTop(i);
        if (r < P.nInfo) {                                   // подсказка
            char l[48]; strlcpy(l, P.info[r], sizeof(l)); fit(l, wA - 2);
            textDraw(1, y + ROW_BASE, l);
            // Справка кончилась, дальше пункты — пунктир, чтобы не путать одно с другим.
            if (r == P.nInfo - 1 && P.n) for (int x = 0; x < wA; x += 2) u8g2.drawPixel(x, y + ROW_H - 1);
            continue;
        }
        int k = r - P.nInfo;
        const Item& it = P.items[k];
        bool sel = P.n && k == v.cursor;
        // Курсор — заливка; рамка — «сейчас нажать нельзя» (защитное окно). Строки
        // справки не выделяются вовсе: курсор по ним только прокручивает список.
        bool info = it.act == AC_NONE;
        bool solid = sel && !info && !guarded(now);
        if (solid) { u8g2.setDrawColor(1); u8g2.drawBox(0, y, wA, ROW_H); u8g2.setDrawColor(0); }
        else if (sel && !info) u8g2.drawFrame(0, y, wA, ROW_H);
        int vw = it.value[0] ? textW(it.value) : 0;
        char l[32]; strlcpy(l, it.label, sizeof(l));
        fit(l, wA - 3 - (vw ? vw + 6 : 0));
        textDraw(2, y + ROW_BASE, l);
        if (vw) textDraw(wA - 2 - vw, y + ROW_BASE, it.value);
        u8g2.setDrawColor(1);
    }
    useSmall();
    if (sb) {
        int t0 = HEADER_H, tH = ROWS * ROW_H;
        for (int j = t0; j < t0 + tH; j += 2) u8g2.drawPixel(126, j);
        int hh = max(4, tH * ROWS / total);
        int yy = t0 + (tH - hh) * v.scroll / (total - ROWS);
        u8g2.drawBox(125, yy, 3, hh);
    }
}
static void drawPage(View& v, uint32_t now) {
    buildFor(v);
    drawHeader(P.title, now);
    drawList(v, now);
}
// Крупное число по левому краю, единица — строчным шрифтом справа от него.
static constexpr int BIG_BASE = 43;
static void drawBigWithUnit(const char* num, const char* unit) {
    u8g2.setFont(FONT_BIG);
    int x = u8g2.drawStr(1, BIG_BASE, num) + 1;
    useRow();
    u8g2.drawUTF8(x + 4, BIG_BASE, unit);
    useSmall();
}
static void drawEdit(View& v, uint32_t now) {
    char title[32]; editTitle(v, title, sizeof(title));
    drawHeader(title, now);
    if (v.id == ED_DOSE_TIME) {
        // М:СС, правится поле под чертой; справа подпись, какое.
        int sec = (int)v.val;
        char m[8], ss[8]; snprintf(m, sizeof(m), "%d", sec / 60); snprintf(ss, sizeof(ss), "%02d", sec % 60);
        u8g2.setFont(FONT_BIG);
        int xm = 1, wm = u8g2.getStrWidth(m);
        int xc = u8g2.drawStr(xm, BIG_BASE, m) + xm;
        int xs = xc + u8g2.drawStr(xc, BIG_BASE, ":");
        int ws = u8g2.drawStr(xs, BIG_BASE, ss);
        if (v.sel == 0) u8g2.drawBox(xm, BIG_BASE + 2, wm, 2); else u8g2.drawBox(xs, BIG_BASE + 2, ws, 2);
        useRow();
        u8g2.drawUTF8(xs + ws + 6, BIG_BASE, v.sel == 0 ? "МИН" : "СЕК");
        useSmall();
    } else {
        char num[16]; snprintf(num, sizeof(num), "%.*f", v.dec, v.val);
        drawBigWithUnit(num, editUnit(v.id));
    }
    if (!v.waiting && (now / 400) % 2) { u8g2.drawTriangle(117, 27, 126, 27, 121, 21); u8g2.drawTriangle(117, 32, 126, 32, 121, 38); }
    useRow();
    char h[40];
    if (v.waiting) { strlcpy(h, "СОХРАНЯЮ", sizeof(h)); for (int i = 0; i < 1 + (int)(now / 300) % 3; i++) strlcat(h, ".", sizeof(h)); }
    else editHint(v, h, sizeof(h));
    fit(h, 127);
    u8g2.drawUTF8(1, 62, h);
    useSmall();
}
static void drawConfirm(View& v, uint32_t now) {
    drawHeader(CF_TEXT[v.id][0], now);
    useRow();
    for (int i = 0; i < 2; i++) {
        char l[40]; strlcpy(l, CF_TEXT[v.id][1 + i], sizeof(l)); fit(l, 127);
        u8g2.drawUTF8(1, rowTop(i) + ROW_BASE, l);
    }
    const char* opt[2] = {"НЕТ", "ДА"};
    const int ox[2] = {10, 74};
    for (int i = 0; i < 2; i++) {
        int w = 44, h = 14, y = 64 - h, tw = u8g2.getUTF8Width(opt[i]);
        if (v.sel == i) { u8g2.drawBox(ox[i], y, w, h); u8g2.setDrawColor(0); }
        else u8g2.drawFrame(ox[i], y, w, h);
        u8g2.drawUTF8(ox[i] + (w - tw) / 2, y + 11, opt[i]);
        u8g2.setDrawColor(1);
    }
    useSmall();
}
static void drawDosing(uint32_t now) {
    char pct[8]; snprintf(pct, sizeof(pct), "%d%%", (int)roundf(S.progress * 100));
    drawHeader("ДОЗИРОВАНИЕ", now);
    char t[12]; mmss(t, sizeof(t), S.elapsedSec);
    u8g2.setFont(FONT_BIG); u8g2.drawStr(1, BIG_BASE, t);
    useRow();
    char l[20];
    snprintf(l, sizeof(l), "A %.1fМЛ", S.volumeA); u8g2.drawUTF8(72, 29, l);
    snprintf(l, sizeof(l), "B %.1fМЛ", S.volumeB); u8g2.drawUTF8(72, 42, l);
    u8g2.drawFrame(0, 46, 128, 5);
    u8g2.drawBox(1, 47, (int)roundf(126 * constrain(S.progress, 0.0f, 1.0f)), 3);
    u8g2.drawUTF8(1, 62, "НАЖМИ: ПРЕРВАТЬ");
    u8g2.drawUTF8(127 - u8g2.getUTF8Width(pct), 62, pct);
    useSmall();
}
static void drawDebug(uint32_t now) {
    drawHeader("РУЧНОЙ", now);
    char num[16]; snprintf(num, sizeof(num), "%.2f", toCm(S.motorPos));
    drawBigWithUnit(num, "СМ");
    if (S.flags & RP_FLAG_MOTOR_BUSY) {
        if (S.motorTarget < S.motorPos) u8g2.drawTriangle(115, 34, 126, 34, 120, 23);
        else                            u8g2.drawTriangle(115, 23, 126, 23, 120, 34);
    }
    useRow();
    char st[16]; snprintf(st, sizeof(st), "ШАГ %.2f", toCm(S.jogSteps));
    u8g2.drawUTF8(1, 62, st);
    useSmall();
    u8g2.drawUTF8(78, 55, "КРУТИ=ХОД");         // концевики — в шапке
    u8g2.drawUTF8(78, 63, "КЛИК=МЕНЮ");
}
static uint32_t sleepAtMs() {
    // Более позднее из двух — через разность, чтобы переживать переполнение millis.
    uint32_t last = (int32_t)(s_lastLinkMs - s_lastInput) > 0 ? s_lastLinkMs : s_lastInput;
    return last + NO_LINK_SLEEP_MS;
}
static void drawLost(uint32_t now) {
    bool connecting = !rlink::everConnected() || now - s_wakeAt < LINK_LOST_MS;
    uint8_t mm = rlink::protoMismatch(now);       // рядом дозатор с другой версией протокола
    drawHeader(mm ? "ВЕРСИИ РАЗНЫЕ" : connecting ? "ПОДКЛЮЧЕНИЕ" : "НЕТ СВЯЗИ", now);
    useRow();
    char l[32], t[12];
    if (mm) {
        u8g2.drawUTF8(1, rowTop(0) + ROW_BASE, "ПРОТОКОЛ НЕ СОВПАДАЕТ");
        snprintf(l, sizeof(l), "ПУЛЬТ v%d, ДОЗАТОР v%d", RP_VERSION, mm);
        u8g2.drawUTF8(1, rowTop(1) + ROW_BASE, l);
        u8g2.drawUTF8(1, rowTop(2) + ROW_BASE, mm > RP_VERSION ? "ОБНОВИ ПУЛЬТ" : "ОБНОВИ ДОЗАТОР");
    } else if (connecting) {
        u8g2.drawUTF8(1, rowTop(0) + ROW_BASE, "ИЩУ ДОЗАТОР");
        snprintf(l, sizeof(l), "КАНАЛ %d", RP_CHANNEL);
        u8g2.drawUTF8(1, rowTop(1) + ROW_BASE, l);
    } else {
        // Дозатор слышен, а наши пакеты до него не доходят — команды не дойдут тоже.
        u8g2.drawUTF8(1, rowTop(0) + ROW_BASE, rlink::downlinkOk(now) ? "КОМАНДЫ НЕ ДОХОДЯТ" : "ДОЗАТОР НЕ ОТВЕЧАЕТ");
        mmss(t, sizeof(t), (now - s_lastLinkMs) / 1000.0f);
        snprintf(l, sizeof(l), "НЕТ СВЯЗИ %s", t);
        u8g2.drawUTF8(1, rowTop(1) + ROW_BASE, l);
        // Кнопка СТОП пульта без связи не доходит — останавливать кнопкой на дозаторе.
        u8g2.drawUTF8(1, rowTop(2) + ROW_BASE, "СТОП - НА ДОЗАТОРЕ");
    }
    u8g2.drawUTF8(1, rowTop(3) + ROW_BASE, "НАЖМИ: ПУЛЬТ");
    if (power::hasBattery()) {                    // от USB не засыпаем — и таймер не нужен
        uint32_t at = sleepAtMs();
        mmss(t, sizeof(t), (int32_t)(at - now) > 0 ? (at - now) / 1000.0f : 0);
        snprintf(l, sizeof(l), "СОН %s", t);
        u8g2.drawUTF8(127 - u8g2.getUTF8Width(l), rowTop(3) + ROW_BASE, l);
    }
    useSmall();
}
// Пока держат СТОП (или энкодер + СТОП) — вместо шапки полоса до срабатывания.
static void drawHoldBar(const char* t, float p) {
    p = constrain(p, 0.0f, 1.0f);
    u8g2.setDrawColor(0); u8g2.drawBox(0, 0, 128, HEADER_H); u8g2.setDrawColor(1);
    u8g2.drawFrame(0, 1, 128, 14);
    u8g2.drawBox(2, 3, (int)roundf(124 * p), 10);
    useRow();
    u8g2.setDrawColor(2);                          // XOR: текст виден и на полосе, и рядом
    u8g2.drawUTF8((128 - u8g2.getUTF8Width(t)) / 2, HEADER_BASE, t);
    u8g2.setDrawColor(1);
    useSmall();
}
static void drawToast() {
    useRow();
    int w = min(126, u8g2.getUTF8Width(s_toast) + 14), x = (128 - w) / 2;
    u8g2.setDrawColor(0); u8g2.drawBox(x, 26, w, 20); u8g2.setDrawColor(1);
    u8g2.drawFrame(x, 26, w, 20);
    u8g2.drawUTF8(x + 7, 26 + 14, s_toast);
    useSmall();
}

static void render(uint32_t now) {
    if (!s_oled) return;
    u8g2.clearBuffer();
    useSmall();
    u8g2.setFontMode(1);
    u8g2.setDrawColor(1);
    if (!rlink::online(now) && !isLocal(top())) {
        drawLost(now);
    } else {
        View& v = top();
        if (v.type == V_HOME && S.screen == RPS_DOSING)     drawDosing(now);
        else if (v.type == V_HOME && S.screen == RPS_DEBUG) drawDebug(now);
        else if (v.type == V_EDIT)                          drawEdit(v, now);
        else if (v.type == V_CONFIRM)                       drawConfirm(v, now);
        else                                                drawPage(v, now);
    }
    if (s_toastOn && (int32_t)(s_toastUntil - now) <= 0) s_toastOn = false;
    if (s_toastOn) drawToast();
    uint32_t held = input::stopHeldMs(now);
    if (held >= PARK_HOLD_SHOW_MS)
        drawHoldBar("ПАРКОВКА", (float)(held - PARK_HOLD_SHOW_MS) / (PARK_HOLD_MS - PARK_HOLD_SHOW_MS));
    uint32_t rh = input::resetHeldMs(now);
    if (rh >= RESET_HOLD_SHOW_MS)
        drawHoldBar("СБРОС НАСТРОЕК", (float)(rh - RESET_HOLD_SHOW_MS) / (RESET_HOLD_MS - RESET_HOLD_SHOW_MS));
    uint32_t combo = input::comboHeldMs(now);
    if (combo >= OTA_HOLD_SHOW_MS)
        drawHoldBar("ОБНОВЛЕНИЕ", (float)(combo - OTA_HOLD_SHOW_MS) / (OTA_HOLD_MS - OTA_HOLD_SHOW_MS));
    // Яркость: в редакторе яркости — то, что крутят сейчас (сразу видно), иначе сохранённая.
    View& tv = top();
    uint8_t want = (tv.type == V_EDIT && tv.id == ED_BRIGHT) ? (uint8_t)tv.val : s_bright;
    if (want != s_brightApplied) applyBright(want);
    u8g2.sendBuffer();
}

// ════════════════════════ Цикл ════════════════════════
// Экран ищем на 0x3C/0x3D. Не нашли — пульт работает без него (радио, СТОП), а
// экран ищем снова каждые 5 с. Итог пишется в Serial при старте и через 3 с
// (USB-порт C3 к началу загрузки часто ещё не открыт).
static char     s_i2cReport[112] = "";
static bool probeOled() {
    for (uint8_t a : {0x3C, 0x3D}) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) { u8g2.setI2CAddress(a * 2); return true; }
    }
    return false;
}
static void oledStart() {
    u8g2.begin();
    u8g2.setBusClock(400000);
    u8g2.enableUTF8Print();
    applyBright(s_bright);
    s_lastFrame = 0;
}

void begin() {
    // Уровни линий до I2C, с подтяжкой к GND: у модуля OLED свои подтяжки к VCC,
    // поэтому 1 — модуль подключён и запитан, 0 — линия никуда не идёт.
    pinMode(PIN_OLED_SDA, INPUT_PULLDOWN); pinMode(PIN_OLED_SCL, INPUT_PULLDOWN);
    delay(2);
    int sda = digitalRead(PIN_OLED_SDA), scl = digitalRead(PIN_OLED_SCL);
    Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
    Wire.setTimeOut(20);
    s_prefs.begin("remote", false);
    s_sleepSec = constrain(s_prefs.getUShort("sleep", SCREEN_SLEEP_DEFAULT_S), 5, 300);
    s_bright = constrain(s_prefs.getUChar("bright", BRIGHT_DEFAULT), 1, 10);
    s_oled = probeOled();
    if (s_oled) {
        snprintf(s_i2cReport, sizeof(s_i2cReport), "OLED found at 0x%02X (SDA=%d SCL=%d)", u8g2.getU8x8()->i2c_address / 2, sda, scl);
        oledStart();
    } else {
        snprintf(s_i2cReport, sizeof(s_i2cReport), "OLED NOT FOUND at 0x3C/0x3D; idle SDA=%d SCL=%d (0 = no pull-up: module not wired or unpowered)", sda, scl);
    }
    Serial.printf("[I] %s\n", s_i2cReport);
    resetHome();
    uint32_t now = millis();
    s_lastInput = s_wakeAt = s_lastLinkMs = now;
}

// ════════════════════════ Режим обновления ════════════════════════
// QR-код для подключения к сети пульта: телефон наводит камеру и подключается сам.
static constexpr int QR_MAX = 33;                 // версия 4 — с запасом для строки WIFI:
static uint8_t s_qr[QR_MAX][(QR_MAX + 7) / 8];
static int     s_qrSize = 0;

static void qrCapture(esp_qrcode_handle_t q) {
    int n = esp_qrcode_get_size(q);
    if (n > QR_MAX) { s_qrSize = 0; return; }
    s_qrSize = n;
    memset(s_qr, 0, sizeof(s_qr));
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++)
            if (esp_qrcode_get_module(q, x, y)) s_qr[y][x / 8] |= 1 << (x % 8);
}

bool otaRequested() { bool r = s_otaRequest; s_otaRequest = false; return r; }

void enterOta(uint32_t now) {
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.display_func = qrCapture;
    cfg.max_qrcode_version = 4;
    cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
    esp_qrcode_generate(&cfg, "WIFI:T:WPA;S:" OTA_SSID ";P:" OTA_PASS ";;");
    if (s_oled && s_screenOff) u8g2.setPowerSave(0);
    s_screenOff = false;
    s_toastOn = false;
}

static void drawOtaWait(const ota::Status& st) {
    // Слева QR (тёмные модули на светлом поле — так его читают все камеры), справа текст.
    if (s_qrSize) {
        int scale = s_qrSize * 2 <= 60 ? 2 : 1;
        int side = s_qrSize * scale, off = (64 - side) / 2;
        u8g2.drawBox(0, 0, 64, 64);
        u8g2.setDrawColor(0);
        for (int y = 0; y < s_qrSize; y++)
            for (int x = 0; x < s_qrSize; x++)
                if (s_qr[y][x / 8] & (1 << (x % 8))) u8g2.drawBox(off + x * scale, off + y * scale, scale, scale);
        u8g2.setDrawColor(1);
    }
    useSmall();
    const int X = 67;
    char first[32];
    strlcpy(first, st.phase == ota::Phase::ERROR ? st.msg : "ЖДУ ФАЙЛ", sizeof(first));
    fit(first, 128 - X);
    u8g2.drawUTF8(X, 7, first);
    u8g2.drawHLine(X, 10, 128 - X);
    // Справа 61 px — 12 символов шрифта 5x7.
    u8g2.drawUTF8(X, 18, OTA_SSID);
    u8g2.drawUTF8(X, 26, OTA_PASS);
    u8g2.drawUTF8(X, 34, "192.168.4.1");
    u8g2.drawUTF8(X, 45, "СТОП ОТКЛ.");      // радио у сети обновления — дозатор не слышит
    u8g2.drawUTF8(X, 54, "ВЫХОД:");
    u8g2.drawUTF8(X, 63, "ДЕРЖИ ЭНК.");
}

static void drawOtaProgress(const ota::Status& st, uint32_t now) {
    drawHeader("ОБНОВЛЕНИЕ", now);
    char n[8];
    if (st.phase == ota::Phase::DONE) {
        useRow();
        u8g2.drawUTF8(1, rowTop(0) + ROW_BASE + 4, "ГОТОВО");
        u8g2.drawUTF8(1, rowTop(2) + ROW_BASE, "ПЕРЕЗАГРУЗКА...");
        useSmall();
        return;
    }
    u8g2.setFont(FONT_BIG);
    snprintf(n, sizeof(n), "%u", st.percent);
    int w = u8g2.getStrWidth(n);
    u8g2.drawStr(0, 44, n);
    useRow();
    u8g2.drawUTF8(w + 3, 44, "%");
    u8g2.drawFrame(0, 50, 128, 6);
    u8g2.drawBox(1, 51, 126 * st.percent / 100, 4);
    useSmall();
    u8g2.drawUTF8(70, 30, "НЕ ВЫКЛЮЧАЙ");
    u8g2.drawUTF8(70, 40, "ПУЛЬТ");
    char v[24]; snprintf(v, sizeof(v), "%s", kFwVersion); fit(v, 128);
    u8g2.drawUTF8(0, 63, v);
}

void tickOta(const input::Events& e, uint32_t now) {
    const ota::Status& st = ota::status();
    // Выход — удержание энкодера; во время записи не выходим.
    if (e.longPress && st.phase != ota::Phase::UPLOAD) ota::exitToNormal("УДЕРЖАНИЕ РУЧКИ");
    if (e.click || e.longPress || e.stop || e.detents)
        Serial.printf("[I] OTA input click=%d long=%d stop=%d det=%d\n", e.click, e.longPress, e.stop, (int)e.detents);
    if (!s_oled || now - s_lastFrame < UI_FRAME_MS) return;
    s_lastFrame = now;
    u8g2.clearBuffer();
    u8g2.setFontMode(1);
    u8g2.setDrawColor(1);
    if (st.phase == ota::Phase::WAIT || st.phase == ota::Phase::ERROR) drawOtaWait(st);
    else drawOtaProgress(st, now);
    if (s_bright != s_brightApplied) applyBright(s_bright);
    u8g2.sendBuffer();
}

bool awake() { return !s_screenOff; }

void alert(const char* text, uint32_t ms) { wake(millis()); showMessage(text, ms); }

void prepareSleep() {
    if (!s_oled) return;
    u8g2.clearBuffer(); u8g2.sendBuffer();
    u8g2.setPowerSave(1);
}

// Глубокий сон — только когда дозатора не видно 2 минуты (и столько же никто не трогал пульт).
// От USB без банки не спим: экономить нечего, а уснувший C3 пропадает из USB и не прошивается.
bool wantsDeepSleep(uint32_t now) {
    return power::hasBattery() && !rlink::online(now) && (int32_t)(now - sleepAtMs()) > 0;
}

void tick(uint32_t now) {
    static bool reported = false;
    if (!reported && now > 3000) { reported = true; Serial.printf("[I] %s\n", s_i2cReport); }
    if (!s_oled && now - s_oledProbeMs > 5000) {
        s_oledProbeMs = now;
        s_oled = probeOled();
        if (s_oled) { oledStart(); Serial.println("[I] OLED appeared"); }
        else Serial.printf("[I] %s\n", s_i2cReport);
    }
    bool on = rlink::online(now);
    if (on) s_lastLinkMs = now;

    // Связь пропала: будим экран (иначе во время дозы он гаснет сразу — его держало
    // только движение) и закрываем диалог: «ДА», выбранное до обрыва, после
    // возврата связи нажимать не должно. Вернулась — защитное окно.
    if (s_wasOnline && !on) {
        wake(now);
        if (top().type == V_CONFIRM && !isLocal(top())) pop();
    } else if (!s_wasOnline && on) {
        guard(now, LINK_GUARD_MS);
    }
    s_wasOnline = on;

    // Сменилось состояние дозатора — домой, будим экран.
    if (on && S.screen != s_lastScreen) {
        uint8_t prev = s_lastScreen;
        s_fromDosing = prev == RPS_DOSING && S.screen == RPS_STOPPED;
        if (top().type == V_EDIT && !top().waiting) showMessage("ПРАВКА ОТМЕНЕНА", TOAST_LONG_MS);
        s_lastScreen = S.screen;
        // Мотор только что встал: курсор оказался на «СТАРТ…/ПАРКОВКА», а палец ещё
        // жмёт «СТОП» — окно длиннее.
        guard(now, prev != 0xFF && movingScreen(prev) && !movingScreen(S.screen) ? MOVE_END_GUARD_MS : SCREEN_GUARD_MS);
        resetHome();
        wake(now);
    }
    if (on && moving() && s_screenOff) wake(now);

    if (s_stopPendingUntil) {
        if ((int32_t)(now - s_stopPendingUntil) > 0) s_stopPendingUntil = 0;
        else if (on) {
            s_stopPendingUntil = 0;
            stopMotor();
        }
    }

    // Редактор, ждущий ACK, закрывается только по OK; при отказе остаётся открытым
    // со своим значением — можно поправить и сохранить ещё раз.
    View& tv = top();
    bool editWait = tv.type == V_EDIT && tv.waiting;
    switch (rlink::takeAck()) {
        case rlink::AckResult::OK: {
            uint8_t t = rlink::lastAckType();
            if (s_saveWait && (t == RP_SET || t == RP_PRESET)) {
                s_saveWait = false;
                if (editWait) pop();
                showMessage("СОХРАНЕНО", TOAST_SHORT_MS);
            }
            break;
        }
        case rlink::AckResult::REJECTED:
            s_saveWait = false; if (editWait) tv.waiting = false;
            showMessage("НЕЛЬЗЯ СЕЙЧАС", TOAST_LONG_MS); break;
        case rlink::AckResult::NO_ANSWER:
            s_saveWait = false; if (editWait) tv.waiting = false;
            showMessage("НЕТ ОТВЕТА", TOAST_LONG_MS); break;
        default: break;
    }
    // СТОП/парковка сбросили очередь, а в ней была настройка: могла и не дойти.
    if (rlink::takeSettingDropped() && s_saveWait) {
        s_saveWait = false;
        if (editWait && top().type == V_EDIT) top().waiting = false;
        showMessage("НЕ ПОДТВЕРЖДЕНО", TOAST_LONG_MS);
    }
    // СТОП не дошёл, потому что связь пропала (а заметили это не сразу) — ждём её,
    // как если бы СТОП нажали уже без связи.
    if (rlink::takeHaltFailed() && !on) s_stopPendingUntil = now + STOP_PENDING_MS;

    if (!s_screenOff && !(on && moving()) && now - s_lastInput > (uint32_t)s_sleepSec * 1000) {
        s_screenOff = true;
        if (top().type == V_CONFIRM) pop();      // диалог не переживает сон экрана
        if (s_oled) u8g2.setPowerSave(1);
    }

    if (!s_screenOff && now - s_lastFrame >= UI_FRAME_MS) {
        s_lastFrame = now;
        render(now);
    }
}

}  // namespace ui
