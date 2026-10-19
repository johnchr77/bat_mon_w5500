/*
 * PIC18F47K42 battery monitor + W5500 web dashboard
 *
 * Browser: http://10.0.0.60/
 *
 * Based on the working program. What is new:
 *   - New dashboard: SOC ring gauge, big voltage, trend arrow, status badge,
 *     cards, "virtual LEDs" that flash like RA1/RA2, and a 1-hour voltage chart.
 *   - W5500 socket 0 now has a 16 KB transmit buffer so a bigger page fits.
 *     The page is streamed into the W5500 in pieces (no big RAM buffers).
 *   - LED lamp test at power-up and an LED polarity switch (LED_ACTIVE_HIGH).
 *   - Optional serial status line for battery_plot.py (SERIAL_STATUS_LINE).
 *   - W5500 start-up: hard reset with VERSIONR check and retry, retry from the
 *     main loop if it was not found, and re-init if the chip loses its settings.

 
// ==========================================
// W5500 Mini Ethernet Module
// PIC18F47K42 — Pin Assignments
// ==========================================

// SPI signals
#define W5500_CS_LAT      LATDbits.LATD4    // Purple: CS
#define W5500_SCK_PIN     PORTCbits.RC6     // Yellow: SCK
#define W5500_MOSI_PIN    PORTCbits.RC4     // Orange: MOSI
#define W5500_MISO_PIN    PORTCbits.RC5     // White: MISO

// Control signals
#define W5500_RST_LAT     LATDbits.LATD5    // Brown: RESET

// Power connections (hardware wiring only)
// Red   = 3.3V
// Green = GND
// INT   = Not connected
```

 *
 * The battery and relay logic is unchanged.
 */

#include <xc.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "mcc_generated_files/system/system.h"
#include "mcc_generated_files/adc/adc.h"
#include "mcc_generated_files/uart/uart2.h"

/* ======================== OPTIONS ======================== */
#define LED_ACTIVE_HIGH     1   /* 1 = LED lights when the pin is HIGH (pin -> resistor -> LED -> GND)
                                   0 = LED lights when the pin is LOW  (3.3V -> resistor -> LED -> pin) */
#define SERIAL_STATUS_LINE  0   /* 1 = print the status line every loop (needed by battery_plot.py) */

/* ======================== BATTERY MONITOR ======================== */
#define ADC_MAX             4095u
#define AVG_SAMPLES         64u
#define DEADBAND            3u
#define VREF_MV             3308u
#define R_TOP_OHM           19950u
#define R_BOT_OHM           5620u
#define CAL_GAIN_PERMILLE   1000u
#define CAL_OFFSET_MV       0

#define V_DEEP              10500u
#define V_LOW               11800u
#define V_HIGH_BAND         13200u

#define V_FULL_MV           13600u
#define V_FULL_CLEAR_MV     13400u
#define V_OVER_FLOAT_MV     14200u
#define V_OVER_HARD_MV      14900u
#define V_SENSE_MIN_MV      6000u
#define V_NO_CHARGER_MV     13000u

#define TICKS_PER_SEC       10u
#define STARTUP_TICKS       (10u * TICKS_PER_SEC)
#define RELAY_MIN_OFF_TICKS (30u * TICKS_PER_SEC)
#define OV_DEBOUNCE_TICKS   (3u * TICKS_PER_SEC)
#define NO_CHARGER_TICKS    (60u * TICKS_PER_SEC)
#define MAX_CHARGE_TICKS    (24UL * 3600UL * TICKS_PER_SEC)
#define TREND_TICKS         (20u * TICKS_PER_SEC)
#define TREND_MV            15u
#define FULL_WINDOWS        2u

/* Voltage history for the chart: one sample about every 30 s, last hour */
#define HIST_N              120u
#define HIST_TICKS          (30u * TICKS_PER_SEC)

#define RELAY_ON()          (LATBbits.LATB0 = 1)
#define RELAY_OFF()         (LATBbits.LATB0 = 0)

static uint8_t soc_from_mv(uint16_t v, uint8_t on_charger)
{
    static const uint16_t rest_mv[5]   = {11900u, 12100u, 12300u, 12600u, 12900u};
    static const uint16_t charge_mv[5] = {12900u, 13125u, 13350u, 13575u, 13800u};
    static const uint8_t pt_pc[5]      = {0u, 25u, 50u, 75u, 100u};
    const uint16_t *pt_mv = on_charger ? charge_mv : rest_mv;
    uint8_t i;

    if (v <= pt_mv[0]) return 0;
    if (v >= pt_mv[4]) return 100;
    for (i = 1; i < 5; ++i) {
        if (v <= pt_mv[i]) {
            uint16_t span_mv = (uint16_t)(pt_mv[i] - pt_mv[i - 1]);
            uint16_t span_pc = (uint16_t)(pt_pc[i] - pt_pc[i - 1]);
            return (uint8_t)(pt_pc[i - 1] +
                ((uint32_t)(v - pt_mv[i - 1]) * span_pc) / span_mv);
        }
    }
    return 100;
}

static uint16_t adc_get_average(void)
{
    uint32_t sum = 0;
    uint8_t i;
    for (i = 0; i < AVG_SAMPLES; ++i) {
        ADC_ChannelSelect(ADC_CHANNEL_ANA0);
        ADC_ConversionStart();
        while (!ADC_IsConversionDone()) { }
        sum += (uint16_t)ADC_ConversionResultGet();
    }
    return (uint16_t)(sum / AVG_SAMPLES);
}

/* ======================== LEDs (RA1 activity, RA2 full) ======================== */
static void Leds_Set(uint8_t a1, uint8_t a2)
{
    uint8_t bits = (uint8_t)((a1 ? 0x02u : 0x00u) | (a2 ? 0x04u : 0x00u));
#if LED_ACTIVE_HIGH
    LATA = (uint8_t)((LATA & 0x01u) | bits);
#else
    LATA = (uint8_t)((LATA & 0x01u) | ((uint8_t)(~bits) & 0x06u));
#endif
}

/* ======================== W5500 HTTP SERVER ======================== */
#define W5500_CS_LOW()      (LATDbits.LATD4 = 0)
#define W5500_CS_HIGH()     (LATDbits.LATD4 = 1)
#define W5500_RST_LOW()     (LATDbits.LATD5 = 0)
#define W5500_RST_HIGH()    (LATDbits.LATD5 = 1)

#define REG_GAR             0x0001
#define REG_SUBR            0x0005
#define REG_SHAR            0x0009
#define REG_SIPR            0x000F
#define REG_PHYCFGR         0x002E
#define REG_VERSIONR        0x0039

#define BSB_COMMON          0x00
#define BSB_S0_REG          0x01
#define BSB_S0_TX           0x02
#define BSB_S0_RX           0x03

#define S0_MR               0x0000
#define S0_CR               0x0001
#define S0_IR               0x0002
#define S0_SR               0x0003
#define S0_PORT             0x0004
#define SN_RXBUF_SIZE       0x001E
#define SN_TXBUF_SIZE       0x001F
#define S0_TX_FSR           0x0020
#define S0_TX_WR            0x0024
#define S0_RX_RSR           0x0026
#define S0_RX_RD            0x0028

#define CMD_OPEN            0x01
#define CMD_LISTEN          0x02
#define CMD_DISCON          0x08
#define CMD_CLOSE           0x10
#define CMD_SEND            0x20
#define CMD_RECV            0x40

#define SOCK_CLOSED         0x00
#define SOCK_INIT           0x13
#define SOCK_LISTEN         0x14
#define SOCK_ESTABLISHED    0x17
#define SOCK_CLOSE_WAIT     0x1C

/* The page is well under this; the page is only built if this much TX space is free */
#define PAGE_RESERVE        6000u

static const uint8_t cfg_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
static const uint8_t cfg_ip[4]  = {10, 0, 0, 60};
static const uint8_t cfg_sub[4] = {255, 255, 255, 0};
static const uint8_t cfg_gw[4]  = {10, 0, 0, 2};

volatile uint8_t w5500_version = 0;
volatile bool w5500_ok = false;
volatile bool cfg_ok = false;
volatile uint8_t phycfgr = 0;
volatile bool link_up = false;
volatile uint8_t socket_status = 0;
volatile bool server_ready = false;
volatile bool http_request_seen = false;
volatile bool send_ok = false;
volatile bool http_response_sent = false;
volatile uint8_t read_ip[4] = {0, 0, 0, 0};
volatile uint16_t received_size = 0;
volatile uint16_t last_response_length = 0;
volatile uint16_t last_tx_free = 0;
volatile uint8_t send_stage = 0;

static char diagnostic[112];

static void UART2_Print(const char *text)
{
    while (*text != '\0') {
        while (!UART2_IsTxReady()) { }
        UART2_Write((uint8_t)*text++);
    }
}

/* Latest battery values shared with the webpage. */
static volatile uint16_t web_vbat_mv = 0;
static volatile uint8_t web_soc = 0;
static volatile uint8_t web_charging = 0;
static volatile uint8_t web_full = 0;
static volatile uint8_t web_fault = 0;
static volatile uint8_t web_no_charger = 0;
static volatile uint8_t web_led1 = 0;   /* 0 off,1 fast,2 slow,3 fault,4 no-charger */
static volatile uint8_t web_led2 = 0;   /* 0 off,1 on,3 fault,5 no-charger */
static char web_state[24] = "STARTING";
static char web_chg[32] = "WAIT";
static char web_trend[8] = "STEADY";

/* Voltage history (ring buffer) for the chart */
static uint16_t hist[HIST_N];
static uint8_t hist_head = 0;
static uint8_t hist_count = 0;

/* Page is streamed into the W5500 TX buffer in pieces. */
static uint16_t tx_ptr = 0;
static uint16_t tx_len = 0;
static char page_buf[700];

static void CopyText(char *dst, uint8_t dst_size, const char *src)
{
    uint8_t i = 0;
    if (dst_size == 0) return;
    while ((i < (uint8_t)(dst_size - 1u)) && (src[i] != '\0')) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static void W5500_Write(uint16_t addr, uint8_t bsb,
                        const uint8_t *data, uint16_t len)
{
    uint16_t i;
    W5500_CS_LOW();
    SPI1_ByteExchange((uint8_t)(addr >> 8));
    SPI1_ByteExchange((uint8_t)(addr & 0xFF));
    SPI1_ByteExchange((uint8_t)((bsb << 3) | 0x04));
    for (i = 0; i < len; i++) SPI1_ByteExchange(data[i]);
    W5500_CS_HIGH();
}

static void W5500_Read(uint16_t addr, uint8_t bsb,
                       uint8_t *data, uint16_t len)
{
    uint16_t i;
    W5500_CS_LOW();
    SPI1_ByteExchange((uint8_t)(addr >> 8));
    SPI1_ByteExchange((uint8_t)(addr & 0xFF));
    SPI1_ByteExchange((uint8_t)(bsb << 3));
    for (i = 0; i < len; i++) data[i] = SPI1_ByteExchange(0x00);
    W5500_CS_HIGH();
}

static void W5500_Write8(uint16_t addr, uint8_t bsb, uint8_t value)
{
    W5500_Write(addr, bsb, &value, 1);
}

static uint8_t W5500_Read8(uint16_t addr, uint8_t bsb)
{
    uint8_t value = 0;
    W5500_Read(addr, bsb, &value, 1);
    return value;
}

static void W5500_Write16(uint16_t addr, uint8_t bsb, uint16_t value)
{
    uint8_t data[2];
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
    W5500_Write(addr, bsb, data, 2);
}

static uint16_t W5500_Read16(uint16_t addr, uint8_t bsb)
{
    uint8_t data[2];
    W5500_Read(addr, bsb, data, 2);
    return ((uint16_t)data[0] << 8) | data[1];
}

static uint16_t W5500_Read16Stable(uint16_t addr, uint8_t bsb)
{
    uint16_t first = 0, second = 0;
    uint8_t tries;
    for (tries = 0; tries < 8; tries++) {
        first = W5500_Read16(addr, bsb);
        second = W5500_Read16(addr, bsb);
        if (first == second) return second;
    }
    return second;
}

static bool W5500_Command(uint8_t command)
{
    uint32_t timeout;
    W5500_Write8(S0_CR, BSB_S0_REG, command);
    for (timeout = 0; timeout < 100000UL; timeout++) {
        if (W5500_Read8(S0_CR, BSB_S0_REG) == 0) return true;
    }
    return false;
}

static bool W5500_Compare(const uint8_t *a, const uint8_t *b, uint8_t len)
{
    uint8_t i;
    for (i = 0; i < len; i++) if (a[i] != b[i]) return false;
    return true;
}

static bool W5500_StartHttpServer(void)
{
    uint8_t port[2] = {0x00, 0x50};
    uint32_t timeout;
    server_ready = false;

    (void)W5500_Command(CMD_CLOSE);
    W5500_Write8(S0_MR, BSB_S0_REG, 0x01); /* TCP */
    W5500_Write(S0_PORT, BSB_S0_REG, port, 2);
    W5500_Write8(S0_IR, BSB_S0_REG, 0xFF);

    if (!W5500_Command(CMD_OPEN)) return false;
    for (timeout = 0; timeout < 50000UL; timeout++) {
        if (W5500_Read8(S0_SR, BSB_S0_REG) == SOCK_INIT) break;
    }
    if (W5500_Read8(S0_SR, BSB_S0_REG) != SOCK_INIT) {
        (void)W5500_Command(CMD_CLOSE);
        return false;
    }
    if (!W5500_Command(CMD_LISTEN)) {
        (void)W5500_Command(CMD_CLOSE);
        return false;
    }
    for (timeout = 0; timeout < 50000UL; timeout++) {
        if (W5500_Read8(S0_SR, BSB_S0_REG) == SOCK_LISTEN) {
            server_ready = true;
            return true;
        }
    }
    (void)W5500_Command(CMD_CLOSE);
    return false;
}

/* ---------- Streaming page into the TX buffer ---------- */
static void TxAppend(const uint8_t *data, uint16_t len)
{
    W5500_Write(tx_ptr, BSB_S0_TX, data, len);
    tx_ptr = (uint16_t)(tx_ptr + len);
    tx_len = (uint16_t)(tx_len + len);
}

static void TxPrint(const char *s)
{
    uint16_t n = 0;
    while (s[n] != '\0') n++;
    TxAppend((const uint8_t *)s, n);
}

/* Static page parts (kept in program memory) */
static const char P_HEAD[] =
    "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n"
    "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta http-equiv='refresh' content='3'><title>Battery</title><style>"
    "*{box-sizing:border-box}body{margin:0;padding:14px;font:15px system-ui,Arial,sans-serif;color:#eef3ff;"
    "background:radial-gradient(circle at 20% 0,#2a4a8a,#0e1730 70%) fixed;min-height:100vh}"
    "main{max-width:640px;margin:auto}h1{font-size:20px;margin:0 0 12px;display:flex;justify-content:space-between;align-items:center}"
    ".b{font-size:12px;padding:5px 11px;border-radius:99px;color:#fff;font-weight:700}"
    ".hero{display:flex;gap:16px;align-items:center;padding:14px;border-radius:18px;background:#ffffff14;border:1px solid #ffffff22;margin-bottom:12px}"
    ".big{font-size:46px;font-weight:800;line-height:1}.u{font-size:18px;opacity:.7}.sub{opacity:.8;margin-top:8px}"
    ".g{display:grid;grid-template-columns:1fr 1fr;gap:10px}.c{padding:12px;border-radius:14px;background:#ffffff12;border:1px solid #ffffff1c;min-width:0}"
    ".l{font-size:11px;letter-spacing:.08em;color:#a9bbe6;margin-bottom:5px}.v{font-size:17px;font-weight:700;overflow-wrap:anywhere}"
    ".w{grid-column:1/3}.led{display:inline-block;width:16px;height:16px;border-radius:50%;background:#34466b;margin-right:8px;vertical-align:middle}"
    ".on{background:#ffd43b;box-shadow:0 0 12px #ffd43b}.f{animation:k .45s infinite}.s{animation:k 1.8s infinite}.d{animation-delay:.9s}"
    "@keyframes k{0%,49%{opacity:1}50%,100%{opacity:.12}}svg{display:block;max-width:100%}"
    ".ft{text-align:center;font-size:12px;color:#8ea2d1;margin:14px 0}"
    "</style></head><body><main>";

static const char P_CHART[] =
    "<div class='c w'><div class='l'>BATTERY VOLTAGE - LAST HOUR</div>"
    "<svg viewBox='0 0 300 100' width='100%'>"
    "<line x1='0' x2='300' y1='34' y2='34' stroke='#2ecc71' stroke-dasharray='4 4'/>"
    "<line x1='0' x2='300' y1='23' y2='23' stroke='#e74c3c' stroke-dasharray='4 4'/>"
    "<text x='2' y='31' font-size='9' fill='#2ecc71'>13.8V float</text>"
    "<text x='2' y='20' font-size='9' fill='#e74c3c'>14.2V trip</text>";

static const char P_NODATA[] =
    "<text x='150' y='65' text-anchor='middle' font-size='11' fill='#a9bbe6'>collecting data...</text>";

static const char P_TAIL[] =
    "</svg></div></div><div class='ft'>refreshes every 3 s</div></main></body></html>";

/* Write the whole page (HTTP header + HTML) into the TX buffer. */
static void BuildPage(void)
{
    const char *ring, *badge, *arrow, *safe_col, *safe_txt;
    const char *r1cls, *r1txt, *r2cls, *r2txt;
    unsigned int soc = web_soc;
    uint16_t b10 = (uint16_t)((web_vbat_mv + 5u) / 10u);   /* centivolts, rounded */
    uint16_t dash = (uint16_t)(((uint32_t)soc * 327u) / 100u);
    uint8_t i, idx;
    int n;

    ring = (soc >= 60u) ? "#2ecc71" : ((soc >= 30u) ? "#f1c40f" : "#e74c3c");

    if (web_fault)                     badge = "#e74c3c";
    else if (web_no_charger)           badge = "#e67e22";
    else if (web_chg[0] == 'C' && web_chg[1] == 'H' && web_chg[2] == 'E') badge = "#e67e22";
    else if (web_charging && web_full) badge = "#2ecc71";
    else if (web_charging)             badge = "#3498db";
    else                               badge = "#68799b";

    if (web_trend[0] == 'U')      arrow = "&#8593; rising";
    else if (web_trend[0] == 'D') arrow = "&#8595; falling";
    else                          arrow = "&#8594; steady";

    safe_col = web_fault ? "#ff6b6b" : "#2ecc71";
    safe_txt = web_fault ? "FAULT" : "NORMAL";

    switch (web_led1) {
        case 1:  r1cls = "on f"; r1txt = "fast flash: voltage rising"; break;
        case 2:  r1cls = "on s"; r1txt = "slow flash: voltage falling"; break;
        case 3:  r1cls = "on f"; r1txt = "fast flash: FAULT"; break;
        case 4:  r1cls = "on s"; r1txt = "slow, alternating: NO CHARGER"; break;
        default: r1cls = "";     r1txt = "off: voltage steady"; break;
    }
    switch (web_led2) {
        case 1:  r2cls = "on";     r2txt = "on: fully charged"; break;
        case 3:  r2cls = "on f";   r2txt = "fast flash: FAULT"; break;
        case 5:  r2cls = "on s d"; r2txt = "slow, alternating: NO CHARGER"; break;
        default: r2cls = "";       r2txt = "off"; break;
    }

    TxPrint(P_HEAD);

    n = sprintf(page_buf,
        "<h1><span>12V Battery Monitor</span><span class='b' style='background:%s'>%s</span></h1>",
        badge, web_chg);
    TxAppend((const uint8_t *)page_buf, (uint16_t)n);

    n = sprintf(page_buf,
        "<div class='hero'><svg width='130' height='130' viewBox='0 0 120 120'>"
        "<circle cx='60' cy='60' r='52' fill='none' stroke='#ffffff25' stroke-width='12'/>"
        "<circle cx='60' cy='60' r='52' fill='none' stroke='%s' stroke-width='12' stroke-linecap='round' "
        "stroke-dasharray='%u 327' transform='rotate(-90 60 60)'/>"
        "<text x='60' y='67' text-anchor='middle' font-size='26' font-weight='700' fill='#fff'>%u%%</text></svg>"
        "<div><div class='big'>%u.%02u<span class='u'> V</span></div><div class='sub'>%s &middot; %s</div></div></div>",
        ring, (unsigned int)dash, soc,
        (unsigned int)(b10 / 100u), (unsigned int)(b10 % 100u),
        arrow, web_state);
    TxAppend((const uint8_t *)page_buf, (uint16_t)n);

    n = sprintf(page_buf,
        "<div class='g'>"
        "<div class='c'><div class='l'>STATUS</div><div class='v'>%s</div></div>"
        "<div class='c'><div class='l'>RELAY</div><div class='v'>%s</div></div>"
        "<div class='c'><div class='l'>FULL</div><div class='v'>%s</div></div>"
        "<div class='c'><div class='l'>SAFETY</div><div class='v' style='color:%s'>%s</div></div>",
        web_state,
        web_charging ? "ON" : "OFF",
        web_full ? "YES" : "NOT YET",
        safe_col, safe_txt);
    TxAppend((const uint8_t *)page_buf, (uint16_t)n);

    n = sprintf(page_buf,
        "<div class='c w'><div class='l'>LEDS ON THE BOARD</div>"
        "<div class='v'><span class='led %s'></span>RA1 &ndash; %s</div>"
        "<div class='v' style='margin-top:8px'><span class='led %s'></span>RA2 &ndash; %s</div></div>",
        r1cls, r1txt, r2cls, r2txt);
    TxAppend((const uint8_t *)page_buf, (uint16_t)n);

    TxPrint(P_CHART);
    if (hist_count < 2u) {
        TxPrint(P_NODATA);
    } else {
        TxPrint("<polyline fill='none' stroke='#4dd0e1' stroke-width='2' points='");
        for (i = 0; i < hist_count; i++) {
            int32_t y;
            uint16_t x;
            idx = (uint8_t)((hist_head + HIST_N - hist_count + i) % HIST_N);
            y = 100 - (((int32_t)hist[idx] - 11500L) * 100L) / 3500L;
            if (y < 0) y = 0;
            if (y > 100) y = 100;
            x = (uint16_t)(((uint32_t)i * 299u) / (uint32_t)(hist_count - 1u));
            n = sprintf(page_buf, "%u,%u ", (unsigned int)x, (unsigned int)y);
            TxAppend((const uint8_t *)page_buf, (uint16_t)n);
        }
        TxPrint("'/>");
    }
    TxPrint(P_TAIL);
}

static bool W5500_SendPage(void)
{
    uint16_t tx_free = 0;
    uint32_t timeout;
    uint8_t interrupt_status;

    send_stage = 1; /* waiting for enough free TX buffer */
    for (timeout = 0; timeout < 50000UL; timeout++) {
        tx_free = W5500_Read16Stable(S0_TX_FSR, BSB_S0_REG);
        last_tx_free = tx_free;
        if (tx_free >= PAGE_RESERVE) break;
    }
    if (tx_free < PAGE_RESERVE) {
        send_stage = 2; /* TX buffer too small (16 KB setting did not apply) */
        return false;
    }

    send_stage = 3; /* building page and issuing SEND */
    tx_ptr = W5500_Read16(S0_TX_WR, BSB_S0_REG);
    tx_len = 0;
    BuildPage();
    last_response_length = tx_len;
    if (tx_len > tx_free) {
        send_stage = 9; /* page larger than free space */
        return false;
    }
    W5500_Write16(S0_TX_WR, BSB_S0_REG, tx_ptr);
    W5500_Write8(S0_IR, BSB_S0_REG, 0xFF);
    if (!W5500_Command(CMD_SEND)) {
        send_stage = 4; /* SEND command did not clear */
        return false;
    }

    send_stage = 5; /* waiting for SEND_OK or TIMEOUT */
    for (timeout = 0; timeout < 100000UL; timeout++) {
        interrupt_status = W5500_Read8(S0_IR, BSB_S0_REG);
        if (interrupt_status & 0x08) {
            W5500_Write8(S0_IR, BSB_S0_REG, 0x08); /* TIMEOUT */
            send_stage = 6; /* W5500 reported transmit timeout */
            return false;
        }
        if (interrupt_status & 0x10) {
            W5500_Write8(S0_IR, BSB_S0_REG, 0x10); /* SEND_OK */
            send_stage = 7; /* success */
            return true;
        }
    }
    send_stage = 8; /* no SEND_OK/TIMEOUT before software timeout */
    return false;
}

/* Hard reset the W5500 and wait until VERSIONR reads 0x04.
   Tries up to 'tries' times. Each try takes about 0.36 s if the chip does not answer. */
static bool W5500_HardReset(uint8_t tries)
{
    static bool spi_open = false;
    uint8_t v = 0;
    uint8_t t;

    if (!spi_open) {
        SPI1_Open(0);
        spi_open = true;
    }
    W5500_CS_HIGH();

    for (t = 0; t < tries; t++) {
        W5500_RST_LOW();
        __delay_ms(5);
        W5500_RST_HIGH();
        __delay_ms(150);               /* PLL lock + PHY start */
        W5500_Read(REG_VERSIONR, BSB_COMMON, &v, 1);
        w5500_version = v;
        if (v == 0x04) return true;
        __delay_ms(200);
    }
    return false;
}

static void W5500_InitializeAndConfigure(uint8_t tries)
{
    uint8_t rb[6];
    bool ok;
    uint8_t n;

    w5500_ok = W5500_HardReset(tries);
    cfg_ok = false;
    server_ready = false;
    if (!w5500_ok) {
        UART2_Print("W5500 not found\r\n");
        return;
    }

    /* Buffer sizes: sockets 1..7 get 0 KB, socket 0 gets 2 KB RX and 16 KB TX.
       Must be done while sockets are closed (right after reset). */
    for (n = 1; n <= 7; n++) {
        uint8_t bsb = (uint8_t)(1u + 4u * n);
        W5500_Write8(SN_RXBUF_SIZE, bsb, 0);
        W5500_Write8(SN_TXBUF_SIZE, bsb, 0);
    }
    W5500_Write8(SN_RXBUF_SIZE, BSB_S0_REG, 2);
    W5500_Write8(SN_TXBUF_SIZE, BSB_S0_REG, 16);

    W5500_Write(REG_SHAR, BSB_COMMON, cfg_mac, 6);
    W5500_Write(REG_SIPR, BSB_COMMON, cfg_ip, 4);
    W5500_Write(REG_SUBR, BSB_COMMON, cfg_sub, 4);
    W5500_Write(REG_GAR, BSB_COMMON, cfg_gw, 4);

    ok = true;
    W5500_Read(REG_SHAR, BSB_COMMON, rb, 6);
    ok = ok && W5500_Compare(rb, cfg_mac, 6);
    W5500_Read(REG_SIPR, BSB_COMMON, rb, 4);
    ok = ok && W5500_Compare(rb, cfg_ip, 4);
    read_ip[0] = rb[0]; read_ip[1] = rb[1];
    read_ip[2] = rb[2]; read_ip[3] = rb[3];
    W5500_Read(REG_SUBR, BSB_COMMON, rb, 4);
    ok = ok && W5500_Compare(rb, cfg_sub, 4);
    W5500_Read(REG_GAR, BSB_COMMON, rb, 4);
    ok = ok && W5500_Compare(rb, cfg_gw, 4);
    cfg_ok = ok;
}

/* Poll socket 0 once. Called repeatedly from the battery main loop. */
static void W5500_WebServerTask(void)
{
    uint8_t rb[1];
    uint16_t rx_size, rx_rd;
    uint8_t status;

    W5500_Read(REG_PHYCFGR, BSB_COMMON, rb, 1);
    phycfgr = rb[0];
    link_up = ((rb[0] & 0x01u) != 0u);

    /* If the chip lost its settings (reset or supply dip), flag it so the
       main loop re-initialises it. */
    {
        uint8_t ip[4];
        W5500_Read(REG_SIPR, BSB_COMMON, ip, 4);
        if (!W5500_Compare(ip, cfg_ip, 4)) {
            w5500_ok = false;
            server_ready = false;
            return;
        }
    }

    status = W5500_Read8(S0_SR, BSB_S0_REG);
    socket_status = status;

    if (!link_up) {
        server_ready = false;
        return;
    }

    if (status == SOCK_CLOSED) {
        server_ready = W5500_StartHttpServer();
        socket_status = W5500_Read8(S0_SR, BSB_S0_REG);
        return;
    }

    if (status == SOCK_LISTEN) {
        server_ready = true;
        return;
    }

    if ((status == SOCK_ESTABLISHED) || (status == SOCK_CLOSE_WAIT)) {
        rx_size = W5500_Read16Stable(S0_RX_RSR, BSB_S0_REG);
        received_size = rx_size;

        if (rx_size > 0u) {
            rx_rd = W5500_Read16(S0_RX_RD, BSB_S0_REG);
            W5500_Write16(S0_RX_RD, BSB_S0_REG, (uint16_t)(rx_rd + rx_size));
            (void)W5500_Command(CMD_RECV);
            http_request_seen = true;

            send_ok = false;
            send_stage = 0;
            send_ok = W5500_SendPage();
            http_response_sent = send_ok;

            (void)sprintf(diagnostic,
                "HTTP rx=%u len=%u txfree=%u sent=%u stage=%u\r\n",
                (unsigned int)rx_size,
                (unsigned int)last_response_length,
                (unsigned int)last_tx_free,
                (unsigned int)send_ok,
                (unsigned int)send_stage);
            UART2_Print(diagnostic);

            if (send_ok) (void)W5500_Command(CMD_DISCON);
            else (void)W5500_Command(CMD_CLOSE);
            server_ready = false;
        } else if (status == SOCK_CLOSE_WAIT) {
            (void)W5500_Command(CMD_CLOSE);
            server_ready = false;
        }
    }
}

/* ======================== ONE MAIN PROGRAM ======================== */
void main(void)
{
    char chg[32];
#if SERIAL_STATUS_LINE
    static char line[140];
#endif

    uint32_t filt = 0;
    uint8_t primed = 0;
    uint16_t shown = 0;

    uint8_t charging = 0;
    uint8_t full = 0;
    uint16_t startup = STARTUP_TICKS;
    uint16_t off_hold = 0;
    uint8_t ov_count = 0;
    uint8_t ov_latched = 0;
    uint8_t nofull_latched = 0;
    uint32_t charge_ticks = 0;
    uint16_t on_ticks = 0;
    uint8_t no_charger = 0;

    uint16_t trend_timer = 0;
    uint16_t trend_ref = 0;
    uint8_t trend_init = 0;
    uint8_t full_windows = 0;
    uint8_t fall_windows = 0;   /* consecutive "falling" windows while the relay is on */
    int8_t trend = 0;
    uint8_t blink = 0;
    uint16_t hist_timer = (uint16_t)(HIST_TICKS - 1u);   /* first sample on the first loop */
    uint8_t w5500_retry = 0;    /* counts loop passes between W5500 re-init attempts */

    SYSTEM_Initialize();

    /* Battery monitor I/O configuration. */
    ANSELA = 0x01;
    TRISA = 0x01;
    LATA = 0x00;
    ANSELBbits.ANSELB0 = 0;
    TRISBbits.TRISB0 = 0;
    RELAY_OFF();
    Leds_Set(0, 0);

    /* W5500 CS and RESET must be digital outputs. */
    ANSELDbits.ANSELD4 = 0;
    ANSELDbits.ANSELD5 = 0;
    TRISDbits.TRISD4 = 0;
    TRISDbits.TRISD5 = 0;
    W5500_CS_HIGH();
    W5500_RST_HIGH();

    /* LED lamp test: RA1, then RA2, then both, then off (about 1.2 s) */
    UART2_Print("LED test: RA1, RA2, both\r\n");
    Leds_Set(1, 0); __delay_ms(400);
    Leds_Set(0, 1); __delay_ms(400);
    Leds_Set(1, 1); __delay_ms(400);
    Leds_Set(0, 0);

    /* Initialize W5500 after system/MCC initialization. Up to 10 attempts at boot. */
    W5500_InitializeAndConfigure(10);
    (void)sprintf(diagnostic, "W5500 ver=0x%02X ok=%u cfg=%u\r\n",
                  (unsigned int)w5500_version, (unsigned int)w5500_ok, (unsigned int)cfg_ok);
    UART2_Print(diagnostic);
    UART2_Print("Battery monitor boot\r\n");

    while (1) {
        uint16_t raw = adc_get_average();
        uint16_t fres, diff, result;
        uint32_t pin_mv;
        int32_t v_mv;
        uint16_t vbat;
        uint8_t nosense;
        uint8_t allowed;
        uint8_t on_charger;
        uint8_t soc;
        const char *state;
        const char *trend_txt;
        uint8_t ra1 = 0, ra2 = 0;
        uint8_t led1_mode = 0, led2_mode = 0;

        /* ---------- Measure and filter ---------- */
        if (!primed) {
            filt = (uint32_t)raw << 4;
            shown = raw;
            primed = 1;
        } else {
            filt = filt - (filt >> 3) + ((uint32_t)raw << 1);
        }
        fres = (uint16_t)(filt >> 4);
        diff = (fres > shown) ? (uint16_t)(fres - shown) : (uint16_t)(shown - fres);
        if (diff > DEADBAND) shown = fres;
        result = shown;
        if (result > ADC_MAX) result = ADC_MAX;

        pin_mv = ((uint32_t)result * VREF_MV) / ADC_MAX;
        v_mv = (int32_t)((((pin_mv * (R_TOP_OHM + R_BOT_OHM)) / R_BOT_OHM) *
                 CAL_GAIN_PERMILLE) / 1000u) + CAL_OFFSET_MV;
        if (v_mv < 0) v_mv = 0;
        vbat = (uint16_t)v_mv;
        nosense = (uint8_t)(vbat < V_SENSE_MIN_MV);

        /* ---------- Overvoltage and watchdog latches ---------- */
        if (vbat > V_OVER_HARD_MV) ov_latched = 1;
        if (vbat > V_OVER_FLOAT_MV) {
            if (ov_count < 255u) ov_count++;
            if (ov_count >= OV_DEBOUNCE_TICKS) ov_latched = 1;
        } else {
            ov_count = 0;
        }

        if (charging && !full && !no_charger) {
            if (charge_ticks < MAX_CHARGE_TICKS) charge_ticks++;
            else nofull_latched = 1;
        } else if (full) {
            charge_ticks = 0;
        }

        /* ---------- Relay decision: fail safe ---------- */
        allowed = (uint8_t)(!nosense && !ov_latched && !nofull_latched);
        if (startup) startup--;
        if (!allowed) {
            charging = 0;
            off_hold = RELAY_MIN_OFF_TICKS;
        } else if (startup) {
            charging = 0;
        } else if (off_hold) {
            off_hold--;
            charging = 0;
        } else {
            charging = 1;
        }
        if (charging) RELAY_ON(); else RELAY_OFF();

        /* ---------- Voltage trend and "full" ---------- */
        if (!trend_init) {
            trend_ref = vbat;
            trend_init = 1;
        }
        if (++trend_timer >= TREND_TICKS) {
            int16_t d;
            trend_timer = 0;
            d = (int16_t)vbat - (int16_t)trend_ref;
            if (d >= (int16_t)TREND_MV) trend = 1;
            else if (d <= -(int16_t)TREND_MV) trend = -1;
            else trend = 0;
            trend_ref = vbat;
            if (charging && trend < 0) {
                if (fall_windows < 255u) fall_windows++;
            } else {
                fall_windows = 0;
            }
            if (charging && vbat >= V_FULL_MV && trend <= 0) {
                if (full_windows < FULL_WINDOWS) full_windows++;
                if (full_windows >= FULL_WINDOWS) full = 1;
            } else {
                full_windows = 0;
            }
        }
        if (!charging || vbat < V_FULL_CLEAR_MV) {
            full = 0;
            full_windows = 0;
        }

        if (charging) {
            if (on_ticks < 65535u) on_ticks++;
        } else {
            on_ticks = 0;
        }
        /* Relay is on but the battery is not being charged:
           - voltage stuck low and not rising for NO_CHARGER_TICKS, or
           - voltage falling for two windows in a row (charger off/unplugged, surface charge fading) */
        no_charger = (uint8_t)(charging &&
                      ((on_ticks >= NO_CHARGER_TICKS && vbat < V_NO_CHARGER_MV && trend <= 0) ||
                       (fall_windows >= 2u)));
        if (no_charger) {
            full = 0;
            full_windows = 0;
        }

        /* ---------- State of charge ---------- */
        on_charger = (uint8_t)(charging && !no_charger);
        soc = soc_from_mv(vbat, on_charger);
        soc = (uint8_t)(((soc + 2u) / 5u) * 5u);

        /* ---------- Voltage history for the chart ---------- */
        if (++hist_timer >= HIST_TICKS) {
            hist_timer = 0;
            hist[hist_head] = vbat;
            hist_head = (uint8_t)((hist_head + 1u) % HIST_N);
            if (hist_count < HIST_N) hist_count++;
        }

        /* ---------- LEDs ---------- */
        blink++;
        if (ov_latched || nofull_latched || nosense) {
            ra1 = ra2 = (blink & 0x02u) ? 1u : 0u;      /* fault: both flash fast together */
            led1_mode = 3;
            led2_mode = 3;
        } else if (no_charger) {
            ra1 = (blink & 0x08u) ? 1u : 0u;            /* no charger: alternate slowly */
            ra2 = (blink & 0x08u) ? 0u : 1u;
            led1_mode = 4;
            led2_mode = 5;
        } else {
            if (trend > 0) {
                ra1 = (blink & 0x02u) ? 1u : 0u;        /* rising: fast flash */
                led1_mode = 1;
            } else if (trend < 0) {
                ra1 = (blink & 0x08u) ? 1u : 0u;        /* falling: slow flash */
                led1_mode = 2;
            }
            if (full || (!charging && soc >= 100u)) {   /* fully charged: RA2 solid */
                ra2 = 1u;
                led2_mode = 1;
            }
        }
        Leds_Set(ra1, ra2);

        /* ---------- Status strings ---------- */
        if      (nosense)             state = "NO SENSE";
        else if (vbat < V_DEEP)       state = "DEEP DISCHARGE";
        else if (vbat < V_LOW)        state = "LOW";
        else if (vbat <= V_HIGH_BAND) state = "REST/OK";
        else if (vbat <= V_OVER_FLOAT_MV) state = (charging && !no_charger) ? "CHARGING" : "SURFACE CHARGE";
        else                          state = "OVERVOLTAGE";

        if      (nosense)             CopyText(chg, sizeof(chg), "FAULT-NO SENSE");
        else if (ov_latched)          CopyText(chg, sizeof(chg), "FAULT-OVERVOLT LATCHED");
        else if (nofull_latched)      CopyText(chg, sizeof(chg), "FAULT-NOT FULL 24H");
        else if (!charging)           CopyText(chg, sizeof(chg), "WAIT");
        else if (no_charger)          CopyText(chg, sizeof(chg), "RELAY ON-NO CHARGER");
        else if (full)                CopyText(chg, sizeof(chg), "FLOAT");
        else if (trend > 0 || vbat >= V_NO_CHARGER_MV) CopyText(chg, sizeof(chg), "CHARGING");
        else                          CopyText(chg, sizeof(chg), "CHECKING CHARGER");

        trend_txt = (trend > 0) ? "UP" : ((trend < 0) ? "DOWN" : "STEADY");

        /* Publish a snapshot for the HTTP page. */
        web_vbat_mv = vbat;
        web_soc = soc;
        web_charging = charging;
        web_full = full;
        web_fault = (uint8_t)(nosense || ov_latched || nofull_latched);
        web_no_charger = no_charger;
        web_led1 = led1_mode;
        web_led2 = led2_mode;
        CopyText(web_state, sizeof(web_state), state);
        CopyText(web_chg, sizeof(web_chg), chg);
        CopyText(web_trend, sizeof(web_trend), trend_txt);

#if SERIAL_STATUS_LINE
        {
            uint16_t v10 = (uint16_t)((vbat + 5u) / 10u);
            uint16_t p10 = (uint16_t)((pin_mv + 5u) / 10u);
            (void)sprintf(line,
                "ADC=%u  Pin=%u.%02uV  Batt=%u.%02uV  SOC~%u%%  %s  RLY=%s  CHG=%s  TREND=%s\r\n",
                (unsigned int)result,
                (unsigned int)(p10 / 100u), (unsigned int)(p10 % 100u),
                (unsigned int)(v10 / 100u), (unsigned int)(v10 % 100u),
                (unsigned int)soc,
                state,
                charging ? "ON" : "OFF",
                chg,
                trend_txt);
            UART2_Print(line);
        }
#endif

        /* Service Ethernet each pass; page is rebuilt when a browser asks.
           If the W5500 was not found or lost its settings, re-initialise it about
           every 3 s (one attempt only, so the battery logic is not blocked for long). */
        if (!w5500_ok || !cfg_ok) {
            if (++w5500_retry >= 30u) {
                w5500_retry = 0;
                W5500_InitializeAndConfigure(1);
            }
        } else {
            W5500_WebServerTask();
        }

        __delay_ms(100);
    }
}