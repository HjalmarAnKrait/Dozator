#include "EspNowLink.h"
#include "WsProtocol.h"
#include "../app/AppState.h"
#include "../app/StateMachine.h"
#include "../app/Calculations.h"
#include "../util/Logger.h"
#include "config.h"
#include "version.h"
#include "RemoteProtocol.h"
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <string.h>

static_assert((uint8_t)Screen::DEBUG == RPS_DEBUG, "Screen и RpScreen разошлись");
static_assert((uint8_t)StopCause::BAD_CHARGE == RPSC_BAD_CHARGE, "StopCause и RpStopCause разошлись");
static_assert((uint8_t)DoneReason::ABORT == RPD_ABORT, "DoneReason и RpDoneReason разошлись");
static_assert(MAX_PRESETS <= RP_MAX_PRESETS, "пресетов больше, чем помещается в STATE");

// ── очередь приёма: колбэк ESP-NOW → loop() ─────────────────────────────────
namespace {
constexpr uint8_t RXQ_SIZE = 6;
constexpr uint8_t RXQ_MAXLEN = 40;   // входящие пакеты короткие (HELLO — 32 байта)
struct RxSlot { uint8_t mac[6]; uint8_t len; uint8_t data[RXQ_MAXLEN]; };
RxSlot            s_rxq[RXQ_SIZE];
volatile uint8_t  s_head = 0, s_tail = 0;

// Пульт с другой версией протокола: его пакеты не разбираем, только запоминаем.
volatile uint8_t  s_mmVersion = 0;
volatile uint32_t s_mmMs = 0;
uint32_t          s_mmReplyMs = 0;
const uint8_t     BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ESP8266 не гарантирует приём второго esp_now_send до завершения первого:
// следующий пакет ждёт колбэка отправки (или 20 мс, если колбэк потерялся).
volatile bool     s_txBusy = false;
uint32_t          s_txMs = 0;
constexpr uint32_t TX_WAIT_MS = 20;
}

void EspNowLink::onSent(uint8_t*, uint8_t) { s_txBusy = false; }

static void txSend(uint8_t* mac, uint8_t* data, uint8_t len) {
    // ACK важнее STATE: если STATE ещё в эфире, всё равно ждём его не дольше пары мс.
    uint32_t t0 = millis();
    while (s_txBusy && millis() - s_txMs < TX_WAIT_MS && millis() - t0 < 3) yield();
    s_txBusy = true;
    s_txMs = millis();
    if (esp_now_send(mac, data, len) != 0) {
        s_txBusy = false;
        LOG_WARN("ESP-NOW send failed (type 0x%02X)", data[2]);
    }
}

void EspNowLink::onRecv(uint8_t* mac, uint8_t* data, uint8_t len) {
    if (!rpValid(data, len)) {
        if (rpOurs(data, len) && data[1] != RP_VERSION) { s_mmVersion = data[1]; s_mmMs = millis(); }
        return;
    }
    if (len > RXQ_MAXLEN) return;
    uint8_t next = (s_head + 1) % RXQ_SIZE;
    if (next == s_tail) return;               // очередь полна — пакет теряется, пульт переспросит
    RxSlot& s = s_rxq[s_head];
    memcpy(s.mac, mac, 6);
    memcpy(s.data, data, len);
    s.len = len;
    s_head = next;
}

bool EspNowLink::begin(WsProtocol* proto, StateMachine* sm) {
    m_proto = proto;
    m_sm    = sm;
    if (esp_now_init() != 0) {
        LOG_ERROR("ESP-NOW init failed");
        return false;
    }
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    esp_now_register_recv_cb(&EspNowLink::onRecv);
    esp_now_register_send_cb(&EspNowLink::onSent);
    esp_now_add_peer((uint8_t*)BROADCAST, ESP_NOW_ROLE_COMBO, RP_CHANNEL, nullptr, 0);   // для RP_VERINFO
    // После перезагрузки seq не должен совпасть со старыми — пульт отбрасывает
    // «устаревшие» STATE по seq. Номер загрузки — по нему пульт видит перезагрузку.
    m_txSeq = (uint8_t)ESP.random();
    do { m_boot = (uint16_t)ESP.random(); } while (m_boot == 0);
    m_ok = true;
    LOG_INFO("ESP-NOW ready, ch=%d, AP MAC %s", RP_CHANNEL, WiFi.softAPmacAddress().c_str());
    return true;
}

bool EspNowLink::remoteOnline(uint32_t nowMs) const {
    return m_hasPeer && (nowMs - m_lastRxMs) < REMOTE_LINK_TIMEOUT_MS;
}

void EspNowLink::setPeer(const uint8_t* mac) {
    if (m_hasPeer && memcmp(m_peer, mac, 6) == 0) return;
    if (m_hasPeer) esp_now_del_peer(m_peer);
    memcpy(m_peer, mac, 6);
    if (!esp_now_is_peer_exist(m_peer))
        esp_now_add_peer(m_peer, ESP_NOW_ROLE_COMBO, RP_CHANNEL, nullptr, 0);
    m_hasPeer = true;
    LOG_INFO("Remote: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void EspNowLink::tick(uint32_t nowMs) {
    if (!m_ok) return;

    while (s_tail != s_head) {
        RxSlot& s = s_rxq[s_tail];
        handle(s.mac, s.data, s.len, nowMs);
        s_tail = (s_tail + 1) % RXQ_SIZE;
    }

    // Пульт с другой версией протокола: сказать ему об этом (раз в секунду, он слушает
    // широковещательно, пока не нашёл дозатор) и показать в веб-интерфейсе.
    uint8_t mm = (s_mmMs && nowMs - s_mmMs < 3000) ? s_mmVersion : 0;
    if (mm != g_state.remoteProtoMismatch) {
        g_state.remoteProtoMismatch = mm;
        if (m_sm) m_sm->requestBroadcast();
        if (mm) LOG_WARN("Remote protocol v%u, ours v%u — update one of them", mm, RP_VERSION);
    }
    if (mm && nowMs - s_mmReplyMs >= 1000) {
        s_mmReplyMs = nowMs;
        RpHeader h;
        rpHeader(h, RP_VERINFO, m_txSeq++, m_boot, 0);
        txSend((uint8_t*)BROADCAST, (uint8_t*)&h, sizeof(h));
    }

    // Пульт появился или пропал — показать это в веб-интерфейсе.
    bool online = remoteOnline(nowMs);
    if (online != m_wasOnline) {
        m_wasOnline = online;
        g_state.remoteOnline = online;
        if (m_sm) m_sm->requestBroadcast();
        LOG_INFO("Remote %s", online ? "online" : "offline");
    }

    if (!online) return;                     // пульт спит или выключен — эфир не засоряем
    uint32_t since = nowMs - m_lastStateMs;
    bool moving = (g_state.screen == Screen::PARKING || g_state.screen == Screen::CHARGING ||
                   g_state.screen == Screen::DOSING  || g_state.screen == Screen::CALIBRATING ||
                   (m_sm && m_sm->motorBusy()));
    uint32_t period = moving ? REMOTE_STATE_MOVING_MS : REMOTE_STATE_IDLE_MS;
    bool due = (m_statePending && since >= REMOTE_STATE_MIN_MS) || since >= period;
    // Предыдущий пакет (обычно ACK) ещё в эфире — STATE уйдёт в следующем проходе.
    if (due && !(s_txBusy && nowMs - s_txMs < TX_WAIT_MS)) sendState(nowMs);
}

void EspNowLink::handle(const uint8_t* mac, const uint8_t* data, uint8_t len, uint32_t nowMs) {
    const RpHeader* h = (const RpHeader*)data;
    if (len < rpMinLen(h->type) || !rpFromRemote(h->type)) return;   // ACK/STATE чужого дозатора — не наш пульт
    // Пока свой пульт на связи, второй его не перехватывает.
    if (m_hasPeer && memcmp(m_peer, mac, 6) != 0 && remoteOnline(nowMs)) return;
    setPeer(mac);
    m_lastRxMs = nowMs;

    // Пульт перезагрузился: его новые seq могут совпасть со старыми из m_seen.
    if (h->boot != m_remoteBoot) {
        if (m_remoteBoot) LOG_INFO("Remote rebooted");
        m_remoteBoot = h->boot;
        memset(m_seen, 0, sizeof(m_seen));
        m_seenNext = 0;
    }

    if (h->type == RP_HELLO) {
        m_helloEcho = h->seq;
        m_statePending = true;
        char fw[sizeof(g_state.remoteFw)];
        memcpy(fw, ((const RpHelloMsg*)data)->fw, sizeof(fw) - 1);   // от пульта нуль в конце не гарантирован
        fw[sizeof(fw) - 1] = 0;
        if (strcmp(g_state.remoteFw, fw) != 0) {
            strcpy(g_state.remoteFw, fw);
            if (m_sm) m_sm->requestBroadcast();
        }
        return;
    }

    // Окно 5 с перекрывает самый долгий повтор пульта (СТОП — 3 с).
    const Seen* dup = nullptr;
    for (const Seen& e : m_seen)
        if (e.ms && e.seq == h->seq && e.type == h->type && nowMs - e.ms < 5000) { dup = &e; break; }
    bool ok;
    if (dup) {
        ok = dup->ok;                      // повтор: тот же ответ, что и в первый раз
    } else if (h->peerBoot != m_boot) {
        // Команда отправлена прошлой загрузке дозатора (мы перезагрузились, пока пульт
        // повторял). Возможно, её уже исполнили до сброса — второй раз не исполняем.
        LOG_INFO("Remote packet 0x%02X for previous boot — rejected", h->type);
        ok = false;
    } else {
        ok = dispatch(data);
        m_seen[m_seenNext] = Seen{h->seq, h->type, ok, nowMs ? nowMs : 1};
        m_seenNext = (m_seenNext + 1) % SEEN_N;
    }
    sendAck(h->seq, h->type, ok);
    m_statePending = true;
}

static const char* cmdName(uint8_t c) {
    switch (c) {
        case RPC_PARK:           return "park";
        case RPC_CALIBRATE:      return "calibrate";
        case RPC_START_CHARGING: return "start_charging";
        case RPC_PUSK:           return "pusk";
        case RPC_ABORT:          return "abort";
        case RPC_NEW_CYCLE:      return "new_cycle";
        case RPC_STOP:           return "stop";
        case RPC_DEBUG:          return "debug";
        case RPC_DEBUG_BACK:     return "debug_back";
        case RPC_JOG_STOP:       return "jog_stop";
        case RPC_HOME:           return "reset";
        default:                 return "";
    }
}

static const char* fieldName(uint8_t f) {
    switch (f) {
        case RPF_DOSE_TIME:    return "doseTimeMin";
        case RPF_A_PRESET:     return "syringeA.presetIdx";
        case RPF_A_DIAM:       return "syringeA.diameter";
        case RPF_B_PRESET:     return "syringeB.presetIdx";
        case RPF_B_DIAM:       return "syringeB.diameter";
        case RPF_PITCH:        return "screwPitch";
        case RPF_PARK_SPEED:   return "parkSpeed";
        case RPF_CHARGE_SPEED: return "chargeSpeed";
        case RPF_H:            return "fullPathSteps";
        case RPF_JOG_STEPS:    return "jogSteps";
        case RPF_JOG_SPEED:    return "jogSpeed";
        default:               return "";
    }
}

bool EspNowLink::dispatch(const uint8_t* data) {
    if (!m_proto) return false;
    // Пишется прошивка дозатора: принимаем только СТОП.
    if (g_state.otaActive) {
        const RpCmdMsg* c = (const RpCmdMsg*)data;
        return c->h.type == RP_CMD && c->cmd == RPC_HALT;
    }
    switch (((const RpHeader*)data)->type) {
        case RP_CMD: {
            RpCmdMsg m; memcpy(&m, data, sizeof(m));
            // Команда нажата на экране, которого у дозатора уже нет (поменялся за
            // время пути или из веба) — не угадываем, что имелось в виду.
            if (m.expect != RPS_ANY && m.expect != (uint8_t)g_state.screen) {
                LOG_INFO("Remote CMD %u: expected screen %u, now %u", m.cmd, m.expect, (uint8_t)g_state.screen);
                return false;
            }
            if (m.cmd == RPC_HALT) {
                // Физическая кнопка СТОП: только остановить то, что едет. В покое — ничего,
                // иначе «stop» в ЗАРЯЖЕНО отменил бы цикл.
                Screen sc = g_state.screen;
                bool cycleMoving = sc == Screen::PARKING || sc == Screen::CHARGING ||
                                   sc == Screen::DOSING  || sc == Screen::CALIBRATING;
                LOG_INFO("Remote HALT");
                if (cycleMoving)              m_proto->handleCommand("stop");
                else if (sc == Screen::DEBUG) m_proto->handleCommand("jog_stop");
                return true;
            }
            LOG_INFO("Remote CMD %s", cmdName(m.cmd));
            return m_proto->handleCommand(cmdName(m.cmd));
        }
        case RP_SET: {
            RpSetMsg m; memcpy(&m, data, sizeof(m));
            if (isnan(m.value)) return false;
            return m_proto->handleDirectSet(fieldName(m.field), m.value, nullptr);
        }
        case RP_PRESET: {
            RpPresetMsg m; memcpy(&m, data, sizeof(m));
            if (isnan(m.vol) || isnan(m.diam)) return false;
            return m_proto->handleSetPreset(m.index, m.vol, m.diam);
        }
        case RP_JOG: {
            RpJogMsg m; memcpy(&m, data, sizeof(m));
            if (g_state.screen != Screen::DEBUG || !m_sm) return false;
            m_sm->jogBy(constrain(m.steps, (int32_t)-100000, (int32_t)100000));
            return true;
        }
        default:
            return false;
    }
}

void EspNowLink::sendAck(uint8_t seq, uint8_t type, bool ok) {
    if (!m_hasPeer) return;
    RpAckMsg m;
    rpHeader(m.h, RP_ACK, m_txSeq++, m_boot, m_remoteBoot);
    m.ackSeq = seq; m.ackType = type; m.ok = ok ? 1 : 0;
    txSend(m_peer, (uint8_t*)&m, sizeof(m));
}

void EspNowLink::sendState(uint32_t nowMs) {
    if (!m_hasPeer) return;
    RpStateMsg m;
    memset(&m, 0, sizeof(m));
    rpHeader(m.h, RP_STATE, m_txSeq++, m_boot, m_remoteBoot);
    m.screen     = (uint8_t)g_state.screen;
    m.stopCause  = (uint8_t)g_state.stopCause;
    m.doneReason = (uint8_t)g_state.doneReason;
    m.calibPhase = g_state.calibPhase;
    m.switches   = (g_state.rawSwitches.top ? RP_SW_TOP : 0) | (g_state.rawSwitches.a ? RP_SW_A : 0) |
                   (g_state.rawSwitches.b ? RP_SW_B : 0)     | (g_state.rawSwitches.bot ? RP_SW_BOT : 0);
    m.flags      = (m_sm && m_sm->motorBusy()) ? RP_FLAG_MOTOR_BUSY : 0;
    m.presetsCount = g_state.presetsCount;
    m.helloEcho  = m_helloEcho;
    m.presetA    = g_state.syringeA.presetIdx;
    m.presetB    = g_state.syringeB.presetIdx;
    m.diamA      = g_state.syringeA.diameter;
    m.diamB      = g_state.syringeB.diameter;
    m.doseTimeMin = g_state.doseTimeMin;
    m.doseVolA   = calc::doseVolA();
    m.doseVolB   = calc::doseVolB();
    m.flowA      = calc::flowA();
    m.flowB      = calc::flowB();
    calc::TimeRange tr = calc::timeRangeMin();
    m.timeMin    = tr.minMin;
    m.timeMax    = tr.maxMin;
    m.screwPitch = g_state.screwPitch;
    m.parkSpeed  = g_state.parkSpeed;
    m.chargeSpeed = g_state.chargeSpeed;
    m.jogSpeed   = g_state.jogSpeed;
    m.fullPathSteps = g_state.fullPathSteps;
    m.motorPos   = g_state.motorPos;
    m.motorTarget = m_sm ? m_sm->motorTarget() : g_state.motorPos;
    m.jogSteps   = g_state.jogSteps;
    m.elapsedSec = g_state.dosing.elapsedSec;
    m.totalSec   = g_state.dosing.totalSec;
    m.progress   = g_state.dosing.progress;
    m.volumeA    = g_state.dosing.volumeA;
    m.volumeB    = g_state.dosing.volumeB;
    for (uint8_t i = 0; i < g_state.presetsCount && i < RP_MAX_PRESETS; i++) {
        m.presets[i].vol  = g_state.presets[i].vol;
        m.presets[i].diam = g_state.presets[i].diam;
    }
    strlcpy(m.fw, kFwVersion, sizeof(m.fw));
    txSend(m_peer, (uint8_t*)&m, sizeof(m));
    m_lastStateMs  = nowMs;
    m_statePending = false;
}
