#pragma once
/**
 * Протокол выносного пульта поверх ESP-NOW.
 * Один файл на обе прошивки: дозатор (ESP8266) и пульт (ESP32-C3).
 * Меняешь здесь — пересобери и прошей ОБЕ платы и подними RP_VERSION.
 *
 * Обмен:
 *   пульт → дозатор: HELLO (раз в 300 мс, всегда), CMD, SET, PRESET, JOG.
 *   дозатор → пульт: STATE (10 Гц при движении, 2 Гц в покое, при изменениях — сразу,
 *                    но не чаще 20 Гц),
 *                    ACK на CMD/SET/PRESET/JOG (принято или отклонено в текущем состоянии).
 *
 * Надёжность команд:
 *   - пульт держит в полёте одну команду, остальные ждут в очереди (толчки
 *     складываются в один); без ACK повторяет с тем же seq, СТОП и парковку —
 *     дольше и вне очереди;
 *   - дозатор помнит последние несколько (seq, type) и на повтор отвечает тем же
 *     ACK, не исполняя команду второй раз;
 *   - CMD несёт экран, который видел пользователь (expect). Если дозатор уже в
 *     другом состоянии, команда отклоняется — она относилась к старому экрану;
 *   - STATE — полный снимок, пульт отбрасывает пришедший позже более свежего;
 *   - в каждом пакете boot — случайный номер загрузки отправителя, peerBoot —
 *     последний услышанный номер загрузки другой стороны. Пульт перезагрузился —
 *     дозатор забывает его старые seq (иначе новая команда могла совпасть со
 *     старой и не исполниться). Дозатор перезагрузился — команды, отправленные
 *     прошлой загрузке (peerBoot не совпал), он отклоняет, а не исполняет заново.
 *
 *
 * Heartbeat в обе стороны: в каждом STATE дозатор возвращает seq последнего
 * услышанного HELLO (helloEcho). Пульт по нему видит, что дозатор слышит пульт,
 * и меряет время ответа. Связь «в порядке» = свежий STATE + свежее эхо HELLO.
 *
 * Версии: первые 8 байт заголовка (magic, version, type, seq, key) не меняются
 * никогда. Пакет с нашими magic и key, но другой версией — это пара с другой
 * прошивкой: дозатор отвечает на него RP_VERINFO, и обе стороны пишут
 * «версии протокола не совпадают», а не просто «нет связи». В HELLO и STATE —
 * строка версии прошивки (для экрана пульта и веб-интерфейса).
 *
 * Поиск: пульт шлёт HELLO на широковещательный адрес, дозатор отвечает на MAC
 * отправителя, пульт запоминает MAC дозатора и дальше шлёт адресно.
 * Чужие пакеты отсекаются по RP_MAGIC + RP_PAIR_KEY. Это защита от случайных
 * пересечений с соседними устройствами, а не криптография.
 */
#include <stdint.h>
#include <string.h>

#ifndef RP_PAIR_KEY
#define RP_PAIR_KEY  0x444F5A31u   // 'DOZ1' — одинаковый у пульта и дозатора; для второй пары поменять
#endif
#define RP_MAGIC     0xD5
#define RP_VERSION   5
#define RP_CHANNEL   1             // канал WiFi: на нём же точка доступа дозатора

enum RpType : uint8_t {
    RP_HELLO  = 0x01,
    RP_CMD    = 0x02,
    RP_SET    = 0x03,
    RP_PRESET = 0x04,
    RP_JOG    = 0x05,
    RP_VERINFO = 0x80,     // только заголовок: «у меня протокол другой версии» (h.version)
    RP_STATE  = 0x81,
    RP_ACK    = 0x82,
};

enum RpCmd : uint8_t {
    RPC_PARK = 1, RPC_CALIBRATE, RPC_START_CHARGING, RPC_PUSK, RPC_ABORT,
    RPC_NEW_CYCLE, RPC_STOP, RPC_DEBUG, RPC_DEBUG_BACK, RPC_JOG_STOP,
    RPC_HOME,          // удержание СТОП на пульте: из любого состояния — на парковку
    RPC_HALT,          // кнопка СТОП: остановить мотор, если он едет; в покое — ничего
};

enum RpField : uint8_t {
    RPF_DOSE_TIME = 1, RPF_A_PRESET, RPF_A_DIAM, RPF_B_PRESET, RPF_B_DIAM,
    RPF_PITCH, RPF_PARK_SPEED, RPF_CHARGE_SPEED, RPF_H, RPF_JOG_STEPS, RPF_JOG_SPEED,
};

// Числа совпадают с enum class Screen / StopCause / DoneReason дозатора (src/app/AppState.h).
enum RpScreen : uint8_t {
    RPS_IDLE = 0, RPS_PARKING, RPS_PARKED, RPS_CHARGING, RPS_CHARGED,
    RPS_DOSING, RPS_DONE, RPS_CALIBRATING, RPS_STOPPED, RPS_DEBUG,
};
enum RpStopCause : uint8_t {
    RPSC_MANUAL = 0, RPSC_STUCK_HOME, RPSC_STUCK_CHARGE, RPSC_STUCK_END, RPSC_CALIB_FAIL, RPSC_BAD_CHARGE,
};
enum RpDoneReason : uint8_t { RPD_TIMER = 0, RPD_BOT, RPD_ABORT };
#define RPS_ANY  0xFF      // expect в CMD: команда годится в любом состоянии

#define RP_SW_TOP  0x01
#define RP_SW_A    0x02
#define RP_SW_B    0x04
#define RP_SW_BOT  0x08

#define RP_FLAG_MOTOR_BUSY  0x01

#define RP_MAX_PRESETS 8

// Диапазон скоростей парковки/зарядки/ручной (шаг/с). Дозатор поджимает те же значения
// (MOTOR_SPS_MIN/MAX в include/config.h, проверка — static_assert в WsProtocol.cpp).
#define RP_SPEED_MIN 50
#define RP_SPEED_MAX 6400

#pragma pack(push, 1)
struct RpHeader {
    uint8_t  magic;
    uint8_t  version;
    uint8_t  type;
    uint8_t  seq;
    uint32_t key;
    uint16_t boot;            // номер загрузки отправителя (случайный, не 0)
    uint16_t peerBoot;        // номер загрузки другой стороны, 0 — ещё не слышали
};
#define RP_FW_LEN  20       // строка версии прошивки с нулём на конце
struct RpHelloMsg  { RpHeader h; char fw[RP_FW_LEN]; };
struct RpCmdMsg    { RpHeader h; uint8_t cmd; uint8_t expect; };   // expect: RpScreen или RPS_ANY
struct RpSetMsg    { RpHeader h; uint8_t field; float value; };
struct RpPresetMsg { RpHeader h; uint8_t index; float vol; float diam; };
struct RpJogMsg    { RpHeader h; int32_t steps; };
struct RpAckMsg    { RpHeader h; uint8_t ackSeq; uint8_t ackType; uint8_t ok; };

struct RpPreset { float vol; float diam; };

struct RpStateMsg {
    RpHeader h;
    uint8_t  screen;          // RpScreen
    uint8_t  stopCause;       // RpStopCause
    uint8_t  doneReason;      // RpDoneReason
    uint8_t  calibPhase;      // 0 = ищем TOP, 1 = меряем ход
    uint8_t  switches;        // RP_SW_* — живые концевики
    uint8_t  flags;           // RP_FLAG_*
    uint8_t  presetsCount;
    uint8_t  helloEcho;       // seq последнего HELLO, услышанного дозатором
    int8_t   presetA, presetB;
    float    diamA, diamB;    // мм
    float    doseTimeMin;
    float    doseVolA, doseVolB;   // мл: объём дозы (в CHARGED — по ходу H − L)
    float    flowA, flowB;         // мл/мин
    float    timeMin, timeMax;     // допустимое время дозы, мин
    float    screwPitch;           // мм/об
    float    parkSpeed, chargeSpeed, jogSpeed;   // шаг/с
    int32_t  fullPathSteps;        // H, 0 = не откалибровано
    int32_t  motorPos, motorTarget, jogSteps;
    float    elapsedSec, totalSec, progress;
    float    volumeA, volumeB;     // мл выдано
    RpPreset presets[RP_MAX_PRESETS];
    char     fw[RP_FW_LEN];        // версия прошивки дозатора
};
#pragma pack(pop)

static_assert(sizeof(RpStateMsg) <= 250, "ESP-NOW: не больше 250 байт в пакете");

inline void rpHeader(RpHeader& h, uint8_t type, uint8_t seq, uint16_t boot = 0, uint16_t peerBoot = 0) {
    h.magic = RP_MAGIC; h.version = RP_VERSION; h.type = type; h.seq = seq; h.key = RP_PAIR_KEY;
    h.boot = boot; h.peerBoot = peerBoot;
}

// Пакеты, которые шлёт только пульт (по ним дозатор узнаёт и запоминает пульт).
inline bool rpFromRemote(uint8_t type) {
    return type == RP_HELLO || type == RP_CMD || type == RP_SET || type == RP_PRESET || type == RP_JOG;
}

// Наш пакет (magic и ключ пары) — любой версии протокола.
inline bool rpOurs(const uint8_t* data, int len) {
    if (len < 8) return false;
    uint32_t key; memcpy(&key, data + 4, 4);
    return data[0] == RP_MAGIC && key == RP_PAIR_KEY;
}

inline bool rpValid(const uint8_t* data, int len) {
    if (len < (int)sizeof(RpHeader)) return false;
    const RpHeader* h = (const RpHeader*)data;
    return h->magic == RP_MAGIC && h->version == RP_VERSION && h->key == RP_PAIR_KEY;
}

// Минимальная длина пакета данного типа (короче — отбрасываем).
inline int rpMinLen(uint8_t type) {
    switch (type) {
        case RP_HELLO:  return sizeof(RpHelloMsg);
        case RP_CMD:    return sizeof(RpCmdMsg);
        case RP_SET:    return sizeof(RpSetMsg);
        case RP_PRESET: return sizeof(RpPresetMsg);
        case RP_JOG:    return sizeof(RpJogMsg);
        case RP_STATE:  return sizeof(RpStateMsg);
        case RP_ACK:    return sizeof(RpAckMsg);
        default:        return 0x7fff;
    }
}
