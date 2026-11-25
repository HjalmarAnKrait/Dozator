#include "link.h"
#include "remote_config.h"
#include "version.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

namespace rlink {

static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ── данные, которые пишет колбэк приёма (задача WiFi) — только под s_mux ──
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static RpStateMsg   s_rxState;
static bool         s_rxStateNew = false;
static uint8_t      s_rxMac[6];
static bool         s_rxAckNew = false;
static RpAckMsg     s_rxAck;
static volatile uint8_t  s_mmVersion = 0;   // дозатор с другой версией протокола
static volatile uint32_t s_mmMs = 0;

// RSSI пакетов дозатора — из promiscuous-колбэка (ESP-NOW сам уровень не отдаёт).
static volatile int8_t s_rssi = 0;
static volatile bool   s_rssiPeerKnown = false;
static uint8_t         s_rssiPeer[6];

static void onPromisc(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT || !s_rssiPeerKnown) return;
    const wifi_promiscuous_pkt_t* p = (const wifi_promiscuous_pkt_t*)buf;
    const uint8_t* hdr = p->payload;
    if (p->rx_ctrl.sig_len < 24 || hdr[0] != 0xD0) return;      // action frame — так ходит ESP-NOW
    if (memcmp(hdr + 10, s_rssiPeer, 6) != 0) return;           // addr2 = отправитель
    s_rssi = p->rx_ctrl.rssi;
}

// ── состояние в главном цикле ──
static RpStateMsg s_state;
static bool       s_have = false;
static uint32_t   s_lastStateMs = 0;
static uint8_t    s_peer[6];
static bool       s_hasPeer = false;
static uint32_t   s_lastHelloMs = 0;
static uint8_t    s_seq = 0;

// Heartbeat: когда ушёл каждый HELLO (по seq) и какой из них дозатор вернул последним.
static uint32_t   s_helloSentMs[256];
static uint32_t   s_echoSentMs = 0;     // когда был отправлен последний подтверждённый HELLO
static uint8_t    s_lastEcho = 0;
static uint32_t   s_rtt = 0;
static bool       s_beat = false;

// Очередь команд. В полёте одна (s_q[0], когда s_inFlight): ждём ACK, повторяем
// с тем же seq, потом берём следующую — так ни одна не теряется и порядок сохраняется.
struct QItem { uint8_t buf[32]; uint8_t len; bool urgent; };
static QItem      s_q[CMD_QUEUE_LEN];
static uint8_t    s_qn = 0;
static bool       s_inFlight = false;
static uint8_t    s_flightSeq = 0;
static uint32_t   s_flightSentMs = 0;
static uint8_t    s_flightRetries = 0;
static AckResult  s_ack = AckResult::NONE;
static bool       s_settingDropped = false;   // СТОП/парковка выкинули из очереди неотправленную настройку
static uint8_t    s_lastStateSeq = 0;
static uint8_t    s_lastAckType = 0;
static bool       s_haltFailed = false;
static uint16_t   s_boot = 0;          // наш номер загрузки
static uint16_t   s_peerBoot = 0;      // номер загрузки дозатора (из STATE)

static void rawSend(const uint8_t* data, size_t len);

static void qPop() {
    if (!s_qn) return;
    memmove(&s_q[0], &s_q[1], sizeof(QItem) * (s_qn - 1));
    s_qn--;
    s_inFlight = false;
}
static void qPump(uint32_t nowMs) {
    if (s_inFlight || !s_qn) return;
    RpHeader* h = (RpHeader*)s_q[0].buf;
    h->seq = s_seq++;                      // seq — в момент первой отправки: дозатор видит их по порядку
    h->boot = s_boot;
    h->peerBoot = s_peerBoot;              // повторы уйдут с тем же: перезагрузившийся дозатор их отклонит
    s_flightSeq = h->seq;
    s_flightSentMs = nowMs;
    s_flightRetries = 0;
    s_inFlight = true;
    rawSend(s_q[0].buf, s_q[0].len);
}

static void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
    if (!rpValid(data, len)) {
        if (rpOurs(data, len) && data[1] != RP_VERSION) { s_mmVersion = data[1]; s_mmMs = millis(); }
        return;
    }
    const RpHeader* h = (const RpHeader*)data;
    if (len < rpMinLen(h->type)) return;
    portENTER_CRITICAL(&s_mux);
    if (h->type == RP_STATE) {
        memcpy(&s_rxState, data, sizeof(s_rxState));
        memcpy(s_rxMac, mac, 6);
        s_rxStateNew = true;
    } else if (h->type == RP_ACK) {
        memcpy(&s_rxAck, data, sizeof(s_rxAck));
        s_rxAckNew = true;
    }
    portEXIT_CRITICAL(&s_mux);
}

static void addPeer(const uint8_t* mac) {
    if (esp_now_is_peer_exist(mac)) return;
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, mac, 6);
    p.channel = RP_CHANNEL;
    p.ifidx   = WIFI_IF_STA;
    p.encrypt = false;
    esp_now_add_peer(&p);
}

void begin() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.setTxPower(REMOTE_TX_POWER);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(RP_CHANNEL, WIFI_SECOND_CHAN_NONE);
    // Promiscuous оставляем включённым только ради RSSI пакетов дозатора.
    wifi_promiscuous_filter_t filt = {};
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(onPromisc);
    if (esp_now_init() != ESP_OK) {
        Serial.println("[E] ESP-NOW init failed");
        return;
    }
    esp_now_register_recv_cb(onRecv);
    addPeer(BROADCAST);
    // После перезагрузки пульта seq не должен совпасть с последним, который дозатор
    // запомнил для отсева повторов, — начинаем со случайного.
    s_seq = (uint8_t)esp_random();
    do { s_boot = (uint16_t)esp_random(); } while (s_boot == 0);
    Serial.printf("[I] ESP-NOW ready, ch=%d, MAC %s\n", RP_CHANNEL, WiFi.macAddress().c_str());
}

void shutdown() {
    esp_wifi_set_promiscuous(false);
    esp_now_deinit();
    WiFi.mode(WIFI_OFF);
}

static void rawSend(const uint8_t* data, size_t len) {
    if (!s_hasPeer) return;
    esp_now_send(s_peer, data, len);
}

void tick(uint32_t nowMs) {
    bool gotState = false, gotAck = false;
    RpAckMsg ack;
    uint8_t mac[6];
    portENTER_CRITICAL(&s_mux);
    static RpStateMsg fresh;
    if (s_rxStateNew) { memcpy(&fresh, &s_rxState, sizeof(fresh)); memcpy(mac, s_rxMac, 6); s_rxStateNew = false; gotState = true; }
    if (s_rxAckNew)   { ack = s_rxAck; s_rxAckNew = false; gotAck = true; }
    portEXIT_CRITICAL(&s_mux);

    // Пока свой дозатор на связи, пакеты другого (с тем же ключом) не берём.
    if (gotState && s_hasPeer && memcmp(s_peer, mac, 6) != 0) gotState = false;
    // Дозатор перезагрузился — его seq начались заново, фильтр устаревших не применяем.
    bool rebooted = gotState && s_peerBoot && fresh.h.boot != s_peerBoot;
    if (gotState && !rebooted && s_have && nowMs - s_lastStateMs < 1000) {
        // Пришёл раньше отправленный снимок после более нового — не откатываемся назад.
        // Большой «скачок назад» — это перезагрузка дозатора, такой берём.
        int8_t d = (int8_t)(fresh.h.seq - s_lastStateSeq);
        if (d <= 0 && d > -32) gotState = false;
    }
    if (gotState) {
        if (rebooted) Serial.println("[I] Dozator rebooted");
        s_peerBoot = fresh.h.boot;
        memcpy(&s_state, &fresh, sizeof(s_state));
        s_lastStateSeq = fresh.h.seq;
        s_have = true;
        s_lastStateMs = nowMs;
        s_beat = !s_beat;
        // Эхо HELLO: дозатор слышит нас. Новое эхо — заодно время ответа.
        uint32_t sent = s_helloSentMs[s_state.helloEcho];
        if (sent && nowMs - sent < 5000) {
            if (s_state.helloEcho != s_lastEcho || !s_echoSentMs) s_rtt = nowMs - sent;
            s_echoSentMs = sent;
        }
        s_lastEcho = s_state.helloEcho;
        if (!s_hasPeer || memcmp(s_peer, mac, 6) != 0) {
            memcpy(s_peer, mac, 6);
            addPeer(s_peer);
            s_rssiPeerKnown = false;
            memcpy(s_rssiPeer, mac, 6);
            s_rssiPeerKnown = true;
            s_hasPeer = true;
            Serial.printf("[I] Dozator %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
    }
    if (gotAck && s_inFlight && ack.ackSeq == s_flightSeq && ack.ackType == ((RpHeader*)s_q[0].buf)->type) {
        s_ack = ack.ok ? AckResult::OK : AckResult::REJECTED;
        s_lastAckType = ack.ackType;
        qPop();
    }

    // Команда без ответа: повторы с тем же seq (дозатор отбросит дубль), потом — «нет ответа».
    // Тогда сбрасываем и всю очередь: команды за ней относились к тому же моменту.
    if (s_inFlight) {
        uint32_t age = nowMs - s_flightSentMs;
        uint32_t limit = s_q[0].urgent ? ACK_FAIL_URGENT_MS : ACK_FAIL_MS;
        if (age >= limit) {
            s_ack = AckResult::NO_ANSWER;
            const RpCmdMsg* c = (const RpCmdMsg*)s_q[0].buf;
            if (c->h.type == RP_CMD && c->cmd == RPC_HALT) s_haltFailed = true;
            s_qn = 0; s_inFlight = false;
        } else if (age >= (uint32_t)ACK_RETRY_MS * (s_flightRetries + 1)) {
            rawSend(s_q[0].buf, s_q[0].len);
            s_flightRetries++;
        }
    }
    qPump(nowMs);

    // Дозатор долго молчит — возможно, перезагрузился или сменился: ищем широковещательно.
    if (s_hasPeer && nowMs - s_lastStateMs > 3 * LINK_LOST_MS) s_hasPeer = false;

    // HELLO идёт всегда, и при погасшем экране: связь держится, пульт сразу видит
    // смену состояния дозатора и пропажу связи.
    if (nowMs - s_lastHelloMs >= HELLO_PERIOD_MS) {
        RpHelloMsg m;
        uint8_t seq = s_seq++;
        rpHeader(m.h, RP_HELLO, seq, s_boot, s_peerBoot);
        strlcpy(m.fw, kFwVersion, sizeof(m.fw));
        s_helloSentMs[seq] = nowMs ? nowMs : 1;
        esp_now_send(s_hasPeer ? s_peer : BROADCAST, (const uint8_t*)&m, sizeof(m));
        s_lastHelloMs = nowMs;
    }
}

bool downlinkOk(uint32_t nowMs)    { return s_have && (nowMs - s_lastStateMs) < LINK_LOST_MS; }
bool uplinkOk(uint32_t nowMs)      { return s_echoSentMs && (nowMs - s_echoSentMs) < LINK_LOST_MS; }
bool online(uint32_t nowMs)        { return downlinkOk(nowMs) && uplinkOk(nowMs); }
bool heartbeat()                   { return s_beat; }
uint8_t protoMismatch(uint32_t nowMs) { return (s_mmMs && nowMs - s_mmMs < 3000) ? s_mmVersion : 0; }
void dozatorFw(char* out, size_t n) {
    if (!s_have) { strlcpy(out, "--", n); return; }
    size_t k = min(n - 1, sizeof(s_state.fw));
    memcpy(out, s_state.fw, k); out[k] = 0;      // нуль в конце от дозатора не гарантирован
}
int8_t rssi()                      { return s_rssi; }
uint32_t rttMs()                   { return s_rtt; }
bool everConnected()               { return s_have; }
uint32_t stateAgeMs(uint32_t nowMs){ return s_have ? nowMs - s_lastStateMs : UINT32_MAX; }
const RpStateMsg& state()          { return s_state; }
const uint8_t* peerMac()           { return s_hasPeer ? s_peer : nullptr; }

AckResult takeAck() { AckResult r = s_ack; s_ack = AckResult::NONE; return r; }
uint8_t lastAckType() { return s_lastAckType; }
bool takeHaltFailed() { bool r = s_haltFailed; s_haltFailed = false; return r; }
bool takeSettingDropped() { bool r = s_settingDropped; s_settingDropped = false; return r; }

static void enqueue(const void* msg, size_t len, bool urgent) {
    if (!s_hasPeer || len > sizeof(QItem::buf)) { s_ack = AckResult::NO_ANSWER; return; }
    const RpHeader* h = (const RpHeader*)msg;
    if (urgent) {
        // СТОП/парковка: всё, что ждало, уже неактуально. Команду в полёте не
        // дожидаемся — она ушла раньше и дойдёт раньше.
        for (uint8_t i = 0; i < s_qn; i++) {
            uint8_t t = ((RpHeader*)s_q[i].buf)->type;
            if (t == RP_SET || t == RP_PRESET) s_settingDropped = true;
        }
        s_qn = 0; s_inFlight = false;
    } else if (h->type == RP_JOG) {
        // Толчки, накопившиеся за время ожидания, складываем в один.
        uint8_t first = s_inFlight ? 1 : 0;
        if (s_qn > first && ((RpHeader*)s_q[s_qn - 1].buf)->type == RP_JOG) {
            RpJogMsg* last = (RpJogMsg*)s_q[s_qn - 1].buf;
            last->steps += ((const RpJogMsg*)msg)->steps;
            return;
        }
    }
    if (s_qn >= CMD_QUEUE_LEN) { s_ack = AckResult::NO_ANSWER; return; }
    QItem& q = s_q[s_qn++];
    memcpy(q.buf, msg, len);
    q.len = len;
    q.urgent = urgent;
    qPump(millis());
}

void sendCmd(uint8_t cmd, uint8_t expect) {
    RpCmdMsg m; rpHeader(m.h, RP_CMD, 0); m.cmd = cmd; m.expect = expect;
    enqueue(&m, sizeof(m), cmd == RPC_HALT || cmd == RPC_HOME);
}
void sendSet(uint8_t field, float value) {
    RpSetMsg m; rpHeader(m.h, RP_SET, 0); m.field = field; m.value = value;
    enqueue(&m, sizeof(m), false);
}
void sendPreset(uint8_t index, float vol, float diam) {
    RpPresetMsg m; rpHeader(m.h, RP_PRESET, 0); m.index = index; m.vol = vol; m.diam = diam;
    enqueue(&m, sizeof(m), false);
}
void sendJog(int32_t steps) {
    RpJogMsg m; rpHeader(m.h, RP_JOG, 0); m.steps = steps;
    enqueue(&m, sizeof(m), false);
}

}  // namespace rlink
