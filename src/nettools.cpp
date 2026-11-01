// nettools.cpp - network toolkit: tools menu, tool page and the tools themselves.
// Tools run in their own task. They touch LVGL only through lvgl_port_lock().
// LVGL callbacks already run with the lock held, so they never take it.
#include <Arduino.h>
#include <stdarg.h>
#include <errno.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"
#include "nettools.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"

#ifndef NET_ICMP
#define NET_ICMP 1      // 1 = real ICMP ping, 0 = TCP-connect fallback
#endif
#ifndef NET_BLE
#define NET_BLE 1       // 1 = Bluetooth scanner, 0 = leave it out
#endif

#if NET_ICMP
#include "ping/ping_sock.h"
#endif
#if NET_BLE
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#endif

// colours: keep in step with UI_DARK in main.cpp
extern bool ui_dark;   // set in main.cpp from the saved theme
#define NT_BG       (ui_dark ? lv_color_hex(0x0f141a) : lv_color_hex(0xffffff))
#define NT_TEXT     (ui_dark ? lv_color_hex(0xe6ebf0) : lv_color_hex(0x1c2530))
#define NT_CHART_BG (ui_dark ? lv_color_hex(0x1a222b) : lv_color_hex(0xf2f4f6))
#define NT_GRID     (ui_dark ? lv_color_hex(0x364352) : lv_color_hex(0xd0d6dc))

// ---------- settings from the web config page ----------
static String g_cfg;
static String g_url;

void nettools_set_config(const String &text) { g_cfg = text; }
void nettools_set_default_url(const char *url) { g_url = url ? url : ""; }

static String cfg_get(const char *key, const String &def = "")
{
    String k = String(key) + "=";
    int pos = 0;
    int len = g_cfg.length();
    while (pos < len) {
        int eol = g_cfg.indexOf('\n', pos);
        if (eol < 0) eol = len;
        String line = g_cfg.substring(pos, eol);
        pos = eol + 1;
        line.trim();
        if (line.startsWith(k)) {
            String v = line.substring(k.length());
            v.trim();
            return v;
        }
    }
    return def;
}

// ---------- small helpers ----------
static String fmt(const char *f, ...)
{
    char buf[400];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return String(buf);
}

static bool resolve(const String &host, IPAddress &ip)
{
    if (ip.fromString(host)) return true;
    return WiFi.hostByName(host.c_str(), ip) == 1;
}

// ---------- tool page state ----------
struct ToolDef {
    const char *group;
    const char *name;
    void (*run)();
    uint8_t visual;     // 0 text only, 1 chart above the text, 2 big picture (QR) above the text
    bool autorun;       // start as soon as the page opens
    const char *hint;
};

static lv_obj_t *p_page = nullptr;
static lv_obj_t *p_title = nullptr;
static lv_obj_t *p_run_lbl = nullptr;
static lv_obj_t *p_box = nullptr;
static lv_obj_t *p_out = nullptr;
static lv_obj_t *p_chart = nullptr;
static lv_chart_series_t *p_ser = nullptr;
static lv_obj_t *p_qr = nullptr;
static int cur = 0;
static volatile bool s_busy = false;
static volatile bool s_cancel = false;
static volatile uint32_t s_gen = 0;
static uint32_t t_gen = 0;

static bool cancelled() { return s_cancel || t_gen != s_gen; }

static void out_set(const String &s)
{
    if (t_gen != s_gen) return;
    lvgl_port_lock(-1);
    lv_label_set_text(p_out, s.c_str());
    lvgl_port_unlock();
}

static void chart_cfg(lv_chart_type_t type, int pts, int ymin, int ymax)
{
    if (t_gen != s_gen) return;
    lvgl_port_lock(-1);
    lv_chart_set_type(p_chart, type);
    lv_chart_set_point_count(p_chart, pts);
    lv_chart_set_range(p_chart, LV_CHART_AXIS_PRIMARY_Y, ymin, ymax);
    lv_chart_set_all_value(p_chart, p_ser, LV_CHART_POINT_NONE);
    lv_chart_refresh(p_chart);
    lvgl_port_unlock();
}

static void chart_range(int ymin, int ymax)
{
    if (t_gen != s_gen) return;
    lvgl_port_lock(-1);
    lv_chart_set_range(p_chart, LV_CHART_AXIS_PRIMARY_Y, ymin, ymax);
    lvgl_port_unlock();
}

static void chart_add(int v)
{
    if (t_gen != s_gen) return;
    lvgl_port_lock(-1);
    lv_chart_set_next_value(p_chart, p_ser, v);
    lvgl_port_unlock();
}

static void chart_at(int i, int v)
{
    if (t_gen != s_gen) return;
    lvgl_port_lock(-1);
    lv_chart_set_value_by_id(p_chart, p_ser, i, v);
    lv_chart_refresh(p_chart);
    lvgl_port_unlock();
}

// ---------- ping helper (ICMP, or TCP fallback) ----------
#if NET_ICMP
static volatile int pg_state = 0;       // 0 waiting, 1 reply, 2 timeout
static volatile uint32_t pg_rtt = 0;

static void pg_ok(esp_ping_handle_t h, void *a)
{
    uint32_t t = 0;
    esp_ping_get_profile(h, ESP_PING_PROF_TIMEGAP, &t, sizeof(t));
    pg_rtt = t;
    pg_state = 1;
}
static void pg_to(esp_ping_handle_t h, void *a) { pg_state = 2; }
static void pg_end(esp_ping_handle_t h, void *a) { if (pg_state == 0) pg_state = 2; }

static bool ping_once(IPAddress ip, uint32_t timeout_ms, uint32_t &rtt)
{
    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    ip_addr_t t;
    IP_ADDR4(&t, ip[0], ip[1], ip[2], ip[3]);
    cfg.target_addr = t;
    cfg.count = 1;
    cfg.timeout_ms = timeout_ms;
    esp_ping_callbacks_t cbs;
    memset(&cbs, 0, sizeof(cbs));
    cbs.on_ping_success = pg_ok;
    cbs.on_ping_timeout = pg_to;
    cbs.on_ping_end = pg_end;
    esp_ping_handle_t h = nullptr;
    pg_state = 0;
    if (esp_ping_new_session(&cfg, &cbs, &h) != ESP_OK) return false;
    esp_ping_start(h);
    uint32_t t0 = millis();
    while (pg_state == 0 && millis() - t0 < timeout_ms + 500) vTaskDelay(pdMS_TO_TICKS(5));
    esp_ping_stop(h);
    esp_ping_delete_session(h);
    rtt = pg_rtt;
    return pg_state == 1;
}
#else
static bool ping_once(IPAddress ip, uint32_t timeout_ms, uint32_t &rtt)
{
    WiFiClient c;
    uint32_t t0 = millis();
    c.connect(ip, 80, timeout_ms);
    rtt = millis() - t0;
    c.stop();
    return rtt < timeout_ms * 9 / 10;   // a refusal comes back fast and still proves the host is alive
}
#endif

// ================= Wi-Fi tools =================
static const char *auth_name(wifi_auth_mode_t a)
{
    switch (a) {
        case WIFI_AUTH_OPEN: return "OPEN";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-E";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
        default: return "?";
    }
}

static void tool_scan()
{
    out_set("Scanning...\n(about 3 seconds)");
    int n = WiFi.scanNetworks(false, true);
    if (cancelled()) { WiFi.scanDelete(); return; }
    if (n <= 0) { out_set("No networks found"); WiFi.scanDelete(); return; }
    String mine = WiFi.BSSIDstr();
    String out = String(n) + " access points  (> = yours)\n";
    int shown = n > 24 ? 24 : n;
    int openCount = 0;
    for (int i = 0; i < shown; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) ssid = "(hidden)";
        if (ssid.length() > 13) ssid = ssid.substring(0, 13);
        wifi_auth_mode_t a = (wifi_auth_mode_t)WiFi.encryptionType(i);
        if (a == WIFI_AUTH_OPEN) openCount++;
        out += String(WiFi.BSSIDstr(i) == mine ? "> " : "  ") + ssid + "  " + String(WiFi.RSSI(i)) +
               "  c" + String(WiFi.channel(i)) + "  " + auth_name(a) + "\n";
    }
    String warn;
    int w = 0;
    for (int i = 0; i < n && w < 3; i++) {
        String si = WiFi.SSID(i);
        if (si.length() == 0 || warn.indexOf("'" + si + "'") >= 0) continue;
        for (int j = i + 1; j < n; j++) {
            if (WiFi.SSID(j) == si && WiFi.encryptionType(j) != WiFi.encryptionType(i)) {
                warn += "! '" + si + "' appears with different security, check it\n";
                w++;
                break;
            }
        }
    }
    out += "\nOpen networks: " + String(openCount) + "\n" + warn;
    if (!w) out += "No look-alike names with mixed security.\n";
    WiFi.scanDelete();
    out_set(out);
}

static void tool_channels()
{
    out_set("Scanning...\n(about 3 seconds)");
    int n = WiFi.scanNetworks();
    if (cancelled()) { WiFi.scanDelete(); return; }
    int cnt[14] = {0};
    float load[14] = {0};
    for (int i = 0; i < n; i++) {
        int ch = WiFi.channel(i);
        if (ch < 1 || ch > 13) continue;
        cnt[ch]++;
        int strength = WiFi.RSSI(i) + 100;
        if (strength < 0) strength = 0;
        for (int k = ch - 2; k <= ch + 2; k++) {
            if (k >= 1 && k <= 13) load[k] += strength;
        }
    }
    int maxc = 4;
    for (int ch = 1; ch <= 13; ch++) if (cnt[ch] > maxc) maxc = cnt[ch];
    chart_cfg(LV_CHART_TYPE_BAR, 13, 0, maxc);
    for (int ch = 1; ch <= 13; ch++) chart_at(ch - 1, cnt[ch]);

    int best = 1;
    for (int ch : {6, 11}) if (load[ch] < load[best]) best = ch;
    String out = "Bars: networks on channels 1 to 13\n\n";
    for (int ch = 1; ch <= 13; ch++) {
        if (cnt[ch]) out += "ch " + String(ch) + ": " + String(cnt[ch]) + (cnt[ch] == 1 ? " network\n" : " networks\n");
    }
    int mine = WiFi.status() == WL_CONNECTED ? WiFi.channel() : 0;
    if (mine) out += "\nYours: ch " + String(mine) + "\n";
    out += "Least crowded of 1/6/11: ch " + String(best) + "\n(2.4 GHz only)";
    WiFi.scanDelete();
    out_set(out);
}

static void tool_signal()
{
    chart_cfg(LV_CHART_TYPE_LINE, 40, -100, -30);
    int mn = 0, mx = -200, n = 0;
    long sum = 0;
    while (!cancelled()) {
        if (WiFi.status() != WL_CONNECTED) {
            out_set("Wi-Fi not connected");
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        int r = WiFi.RSSI();
        if (n == 0 || r < mn) mn = r;
        if (r > mx) mx = r;
        sum += r;
        n++;
        const char *q = r >= -55 ? "excellent" : r >= -65 ? "good" : r >= -75 ? "fair" : "weak";
        chart_add(r);
        out_set(fmt("Now: %d dBm (%s)\nMin %d   Avg %d   Max %d\n\nWalk around with the device.\nDips in the line are dead spots.",
                    r, q, mn, (int)(sum / n), mx));
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ================= LAN tools =================
static void tool_netinfo()
{
    if (WiFi.status() != WL_CONNECTED) { out_set("Wi-Fi not connected"); return; }
    out_set(fmt("IP: %s\nMask: %s\nGateway: %s\nDNS 1: %s\nDNS 2: %s\nMAC: %s\nAP (BSSID): %s\nChannel: %d\nSignal: %d dBm\nName: " DEVICE_HOST ".local",
                WiFi.localIP().toString().c_str(), WiFi.subnetMask().toString().c_str(),
                WiFi.gatewayIP().toString().c_str(), WiFi.dnsIP(0).toString().c_str(),
                WiFi.dnsIP(1).toString().c_str(), WiFi.macAddress().c_str(),
                WiFi.BSSIDstr().c_str(), (int)WiFi.channel(), (int)WiFi.RSSI()));
}

static void tool_ping()
{
    String t = cfg_get("target", WiFi.gatewayIP().toString());
    IPAddress ip;
    if (!resolve(t, ip)) { out_set("Cannot resolve '" + t + "'\nSet target= in Tools settings."); return; }
    chart_cfg(LV_CHART_TYPE_LINE, 30, 0, 100);
    int ymax = 100, sent = 0, got = 0;
    uint32_t mn = 999999, mx = 0, last = 0;
    double sum = 0, jit = 0;
    while (!cancelled()) {
        uint32_t t0 = millis();
        uint32_t rtt = 0;
        bool ok = ping_once(ip, 1000, rtt);
        sent++;
        if (ok) {
            got++;
            sum += rtt;
            if (rtt < mn) mn = rtt;
            if (rtt > mx) mx = rtt;
            if (got > 1) jit += abs((int)rtt - (int)last);
            last = rtt;
            if ((int)rtt > ymax) {
                ymax = (((int)rtt * 12 / 10 + 49) / 50) * 50;
                chart_range(0, ymax);
            }
            chart_add((int)rtt);
        } else {
            chart_add(LV_CHART_POINT_NONE);
        }
        String line = "Target: " + ip.toString() + "\n";
        line += fmt("Sent %d  Got %d  Loss %d%%\n", sent, got, (sent - got) * 100 / sent);
        if (got) {
            line += fmt("Last %lu  Min %lu  Avg %d  Max %lu ms\nJitter %d ms",
                        (unsigned long)last, (unsigned long)mn, (int)(sum / got), (unsigned long)mx,
                        got > 1 ? (int)(jit / (got - 1)) : 0);
        } else {
            line += "No replies yet";
        }
        out_set(line);
        uint32_t el = millis() - t0;
        if (el < 1000) vTaskDelay(pdMS_TO_TICKS(1000 - el));
    }
}

static void tool_hosts()
{
    if (WiFi.status() != WL_CONNECTED) { out_set("Wi-Fi not connected"); return; }
    IPAddress me = WiFi.localIP();
    IPAddress gwip = WiFi.gatewayIP();
    String out;
    int found = 0;
    for (int h = 1; h <= 254 && !cancelled(); h++) {
        if (h == me[3]) continue;
        IPAddress ip(me[0], me[1], me[2], h);
        uint32_t rtt = 0;
        if (ping_once(ip, 150, rtt)) {
            found++;
            out += ip.toString() + "  " + String((unsigned long)rtt) + " ms" + (ip == gwip ? "  (router)" : "") + "\n";
        }
        if (h % 4 == 0 || h == 254) {
            out_set(fmt("Scanning %u.%u.%u.%d  (%d/254)\nFound: %d\n\n", (unsigned)me[0], (unsigned)me[1], (unsigned)me[2], h, h, found) + out);
        }
    }
    if (!cancelled()) {
        out_set(fmt("Done: %d hosts answered.\nDevices that block ping do not show.\n(scans this /24 only)\n\n", found) + out);
    }
}

static const char *port_name(uint16_t p)
{
    switch (p) {
        case 21: return "FTP";      case 22: return "SSH";     case 23: return "Telnet";
        case 25: return "SMTP";     case 53: return "DNS";     case 80: return "HTTP";
        case 110: return "POP3";    case 139: return "NetBIOS"; case 443: return "HTTPS";
        case 445: return "SMB";     case 1883: return "MQTT";  case 3306: return "MySQL";
        case 3389: return "RDP";    case 5900: return "VNC";   case 8080: return "HTTP-alt";
        case 8883: return "MQTT/TLS";
        default: return "";
    }
}

static void tool_ports()
{
    String t = cfg_get("target", WiFi.gatewayIP().toString());
    IPAddress ip;
    if (!resolve(t, ip)) { out_set("Cannot resolve '" + t + "'\nSet target= in Tools settings."); return; }
    static const uint16_t ports[] = {21, 22, 23, 25, 53, 80, 110, 139, 443, 445, 1883, 3306, 3389, 5900, 8080, 8883};
    const int N = sizeof(ports) / sizeof(ports[0]);
    String open;
    int nopen = 0, unreach = 0;
    for (int i = 0; i < N && !cancelled(); i++) {
        out_set(fmt("Checking %s\nport %u  (%d/%d)\n\nOpen so far: %d\n", ip.toString().c_str(), (unsigned)ports[i], i + 1, N, nopen) + open);
        WiFiClient c;
        errno = 0;
        bool ok = c.connect(ip, ports[i], 500);
        int err = errno;
        c.stop();
        if (!ok && (err == EHOSTUNREACH || err == ENETUNREACH)) unreach++;
        if (ok) {
            nopen++;
            open += String(ports[i]) + "  " + port_name(ports[i]) + "\n";
        }
    }
    if (!cancelled() && unreach == N) {
        out_set("Host " + ip.toString() + " is unreachable.\n\nNo connection could even start.\nCheck the address, or whether this\nWi-Fi isolates devices from each other.");
        return;
    }
    if (!cancelled()) {
        out_set("Host " + ip.toString() + "\n" + (nopen ? String(nopen) + " open:\n" + open : String("No common ports open")) +
                "\n\nOnly scan hosts you own or manage.");
    }
}

static void tool_mdns()
{
    static const char *types[] = {"http", "mqtt", "ssh", "ipp", "smb", "workstation"};
    const int N = sizeof(types) / sizeof(types[0]);
    String out;
    int total = 0;
    for (int i = 0; i < N && !cancelled(); i++) {
        out_set(String("Looking for _") + types[i] + "._tcp  (" + String(i + 1) + "/" + String(N) + ")\n\n" + out);
        int n = MDNS.queryService(types[i], "tcp");
        for (int j = 0; j < n; j++) {
            String hn = MDNS.hostname(j);
            out += String(types[i]) + ": " + hn + "  " + MDNS.address(j).toString() + ":" + String(MDNS.port(j)) + "\n";
            total++;
        }
    }
    if (!cancelled()) out_set(String(total) + " services found\n\n" + out);
}

static bool parse_mac(const String &s, uint8_t *m)
{
    int digits = 0;
    uint8_t cur_byte = 0;
    for (size_t i = 0; i < s.length(); i++) {
        char ch = s[i];
        int v;
        if (ch >= '0' && ch <= '9') v = ch - '0';
        else if (ch >= 'a' && ch <= 'f') v = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') v = ch - 'A' + 10;
        else continue;                                  // skip ':' and '-'
        if (digits >= 12) return false;
        cur_byte = (uint8_t)((cur_byte << 4) | v);
        if (digits % 2 == 1) m[digits / 2] = cur_byte;
        digits++;
    }
    return digits == 12;
}

static void tool_wol()
{
    String mac = cfg_get("wol", "");
    uint8_t m[6];
    if (!parse_mac(mac, m)) {
        out_set("No MAC address set.\n\nOn the web config page, in Tools settings, add:\nwol=AA:BB:CC:DD:EE:FF");
        return;
    }
    uint8_t pkt[102];
    memset(pkt, 0xFF, 6);
    for (int i = 0; i < 16; i++) memcpy(pkt + 6 + i * 6, m, 6);
    IPAddress ip = WiFi.localIP(), mask = WiFi.subnetMask();
    IPAddress bc(ip[0] | (uint8_t)~mask[0], ip[1] | (uint8_t)~mask[1], ip[2] | (uint8_t)~mask[2], ip[3] | (uint8_t)~mask[3]);
    WiFiUDP udp;
    for (int k = 0; k < 2; k++) {
        udp.beginPacket(bc, 9);
        udp.write(pkt, sizeof(pkt));
        udp.endPacket();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    out_set("Magic packet sent to\n" + mac + "\nvia " + bc.toString() + ":9\n\nThe PC must have Wake-on-LAN\nswitched on in its BIOS and adapter.");
}

// ================= Internet tools =================
static void tool_latency()
{
    out_set("Testing...");
    uint32_t rtt = 0;
    String out;
    IPAddress g = WiFi.gatewayIP();
    int gOk = 0;
    uint32_t gSum = 0;
    for (int k = 0; k < 3 && !cancelled(); k++) if (ping_once(g, 800, rtt)) { gOk++; gSum += rtt; }
    out += "Router " + g.toString() + ":\n  " + (gOk ? String(gSum / gOk) + " ms avg (" + String(gOk) + "/3)" : String("no reply")) + "\n\n";
    out_set(out + "Testing DNS...");

    static const char *names[] = {"wikipedia.org", "github.com", "cloudflare.com"};
    uint32_t dSum = 0;
    int dOk = 0;
    for (int k = 0; k < 3 && !cancelled(); k++) {
        IPAddress ip;
        uint32_t t0 = millis();
        if (WiFi.hostByName(names[k], ip) == 1) { dSum += millis() - t0; dOk++; }
    }
    out += "DNS (3 lookups):\n  " + (dOk ? String(dSum / dOk) + " ms avg" : String("failed")) + "\n\n";
    out_set(out + "Testing internet...");

    unsigned long mn = 999999, mx = 0, sum = 0;
    int tOk = 0;
    for (int k = 0; k < 3 && !cancelled(); k++) {
        WiFiClient c;
        uint32_t t0 = millis();
        bool ok = c.connect(IPAddress(1, 1, 1, 1), 443, 3000);
        unsigned long d = millis() - t0;
        c.stop();
        if (ok) { tOk++; sum += d; if (d < mn) mn = d; if (d > mx) mx = d; }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    out += "TCP 1.1.1.1:443:\n  ";
    out += tOk ? ("min " + String(mn) + "  avg " + String(sum / tOk) + "  max " + String(mx)) : String("failed");
    out += "\n\nSignal: " + String(WiFi.RSSI()) + " dBm";
    if (!cancelled()) out_set(out);
}

static void tool_speed()
{
    String url = cfg_get("speedurl", "https://speed.cloudflare.com/__down?bytes=2000000");
    out_set("Connecting...");
    HTTPClient http;
    WiFiClientSecure sc;
    WiFiClient pc;
    if (url.startsWith("https")) { sc.setInsecure(); http.begin(sc, url); }
    else http.begin(pc, url);
    http.setTimeout(10000);
    uint32_t t0 = millis();
    int code = http.GET();
    uint32_t ttfb = millis() - t0;
    if (code <= 0) {
        out_set(fmt("Request failed (%d)\n%s", code, HTTPClient::errorToString(code).c_str()));
        http.end();
        return;
    }
    int len = http.getSize();
    WiFiClient *st = http.getStreamPtr();
    uint8_t buf[1024];
    size_t total = 0;
    uint32_t t1 = millis(), lastShow = 0;
    while (http.connected() && (len > 0 || len == -1) && !cancelled() && millis() - t1 < 20000) {
        size_t av = st->available();
        if (av) {
            int r = st->readBytes(buf, av < sizeof(buf) ? av : sizeof(buf));
            if (r > 0) { total += r; if (len > 0) len -= r; }
        } else {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        if (millis() - lastShow > 500) {
            lastShow = millis();
            out_set(fmt("Downloading...\n%u KB so far", (unsigned)(total / 1024)));
        }
    }
    uint32_t ms = millis() - t1;
    http.end();
    if (cancelled()) return;
    double mbps = ms ? (total * 8.0) / ms / 1000.0 : 0;
    out_set(fmt("HTTP %d, first byte %lu ms\nDownloaded %u KB in %lu ms\n\nSpeed: %.2f Mbit/s\n\nRough figure: this chip's Wi-Fi\nand TLS limit it well below\na normal phone or laptop.",
                code, (unsigned long)ttfb, (unsigned)(total / 1024), (unsigned long)ms, mbps));
}

static void set_dns_both(IPAddress ip)
{
    ip_addr_t a;
    IP_ADDR4(&a, ip[0], ip[1], ip[2], ip[3]);
    dns_setserver(0, &a);
    dns_setserver(1, &a);
}

static void tool_dns()
{
    struct Res { const char *name; IPAddress ip; };
    Res list[4];
    int cnt = 0;
    IPAddress router = WiFi.dnsIP(0);
    if (router != IPAddress(0, 0, 0, 0)) list[cnt++] = {"Router", router};
    list[cnt++] = {"Cloudflare", IPAddress(1, 1, 1, 1)};
    list[cnt++] = {"Google", IPAddress(8, 8, 8, 8)};
    list[cnt++] = {"Quad9", IPAddress(9, 9, 9, 9)};

    ip_addr_t save0 = *dns_getserver(0);
    ip_addr_t save1 = *dns_getserver(1);
    String out = "Uncached lookups, 3 per server.\nA name that cannot exist must fail;\nan answer means DNS hijacking.\n\n";
    bool hijack = false;
    for (int s = 0; s < cnt && !cancelled(); s++) {
        out_set(out + "Testing " + list[s].name + "...");
        set_dns_both(list[s].ip);
        unsigned long sum = 0;
        int got = 0, bad = 0;
        for (int k = 0; k < 3 && !cancelled(); k++) {
            char host[40];
            snprintf(host, sizeof(host), "pn%08x.com", (unsigned)esp_random());
            IPAddress r;
            uint32_t t0 = millis();
            int ok = WiFi.hostByName(host, r);
            uint32_t ms = millis() - t0;
            if (ok == 1) bad++;
            if (ms < 2500) { sum += ms; got++; }
        }
        out += String(list[s].name) + " " + list[s].ip.toString() + ":\n  ";
        out += got ? String(sum / got) + " ms avg" : String("no answer");
        if (bad) { out += "  HIJACK!"; hijack = true; }
        out += "\n";
    }
    dns_setserver(0, &save0);
    dns_setserver(1, &save1);
    if (cancelled()) return;
    out += hijack ? "\nA resolver answers for names that\ndo not exist: it rewrites failed\nlookups (ISP or router)." : "\nNo hijacking seen.";
    out_set(out);
}

static String jget(const String &js, const char *key)
{
    int i = js.indexOf(String("\"") + key + "\"");
    if (i < 0) return "";
    i = js.indexOf(':', i);
    int a = js.indexOf('"', i);
    int b = js.indexOf('"', a + 1);
    if (i < 0 || a < 0 || b < 0) return "";
    return js.substring(a + 1, b);
}

static void tool_publicip()
{
    out_set("Asking ipinfo.io...");
    WiFiClientSecure sc;
    sc.setInsecure();
    HTTPClient http;
    http.setTimeout(8000);
    http.begin(sc, "https://ipinfo.io/json");
    int code = http.GET();
    if (code == 200) {
        String js = http.getString();
        http.end();
        out_set("Public IP: " + jget(js, "ip") + "\nCity: " + jget(js, "city") + "\nRegion: " + jget(js, "region") +
                "\nCountry: " + jget(js, "country") + "\nProvider: " + jget(js, "org"));
        return;
    }
    http.end();
    WiFiClient pc;
    HTTPClient h2;
    h2.setTimeout(8000);
    h2.begin(pc, "http://api.ipify.org");
    code = h2.GET();
    if (code == 200) out_set("Public IP: " + h2.getString());
    else out_set(fmt("Lookup failed (%d)", code));
    h2.end();
}

static bool read_line(Client *c, String &line, uint32_t timeout_ms)
{
    line = "";
    uint32_t t0 = millis();
    while (millis() - t0 < timeout_ms) {
        if (cancelled()) return false;
        if (c->available()) {
            int ch = c->read();
            if (ch < 0) continue;
            if (ch == '\n') return true;
            if (ch != '\r' && line.length() < 120) line += (char)ch;
        } else {
            if (!c->connected()) return line.length() > 0;
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
    return false;
}

static void tool_http()
{
    String url = cfg_get("url", g_url);
    if (url.length() == 0) url = "https://example.com/";
    bool https = url.startsWith("https://");
    if (!https && !url.startsWith("http://")) { out_set("URL must start with http:// or https://"); return; }
    int p = url.indexOf("://") + 3;
    int slash = url.indexOf('/', p);
    String hostport = slash < 0 ? url.substring(p) : url.substring(p, slash);
    String path = slash < 0 ? String("/") : url.substring(slash);
    String host = hostport;
    uint16_t port = https ? 443 : 80;
    int colon = hostport.indexOf(':');
    if (colon >= 0) { host = hostport.substring(0, colon); port = (uint16_t)hostport.substring(colon + 1).toInt(); }

    out_set("Testing\n" + url);
    uint32_t t0 = millis();
    IPAddress ip;
    bool dnsOk = resolve(host, ip);
    uint32_t dnsMs = millis() - t0;
    if (!dnsOk) { out_set("DNS failed for " + host); return; }

    WiFiClient probe;
    t0 = millis();
    bool tcpOk = probe.connect(ip, port, 5000);
    uint32_t tcpMs = millis() - t0;
    probe.stop();
    if (!tcpOk) {
        out_set(fmt("DNS %lu ms\nTCP connect to %s:%u failed", (unsigned long)dnsMs, ip.toString().c_str(), (unsigned)port));
        return;
    }

    WiFiClient plain;
    WiFiClientSecure sec;
    sec.setInsecure();
    Client *c = https ? (Client *)&sec : (Client *)&plain;
    t0 = millis();
    bool ok = https ? (bool)sec.connect(host.c_str(), port) : (bool)plain.connect(ip, port, 5000);
    uint32_t connMs = millis() - t0;
    if (!ok) { out_set("Connection failed\n(TLS handshake or refused)"); return; }
    uint32_t tlsMs = (https && connMs > tcpMs) ? connMs - tcpMs : 0;

    c->print("GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nUser-Agent: " DEVICE_NAME "\r\nConnection: close\r\n\r\n");
    t0 = millis();
    while (!c->available() && c->connected() && millis() - t0 < 8000 && !cancelled()) vTaskDelay(pdMS_TO_TICKS(2));
    uint32_t ttfb = millis() - t0;

    String status, line, extra;
    if (read_line(c, status, 4000)) {
        for (int i = 0; i < 40; i++) {
            if (!read_line(c, line, 3000) || line.length() == 0) break;
            String low = line;
            low.toLowerCase();
            if (low.startsWith("server:") || low.startsWith("content-type:") || low.startsWith("content-length:") ||
                low.startsWith("location:") || low.startsWith("cache-control:")) {
                extra += line + "\n";
            }
        }
    }
    c->stop();
    if (cancelled()) return;
    out_set("Timing for " + host + "\n" + fmt("DNS   %lu ms\nTCP   %lu ms\n", (unsigned long)dnsMs, (unsigned long)tcpMs) +
            (https ? fmt("TLS   ~%lu ms\n", (unsigned long)tlsMs) : String("")) +
            fmt("Wait  %lu ms (first byte)\n\n", (unsigned long)ttfb) + status + "\n" + extra);
}

// ================= IoT tools =================
static int mq_len(uint8_t *b, int n)
{
    int i = 0;
    do {
        uint8_t d = n % 128;
        n /= 128;
        if (n > 0) d |= 0x80;
        b[i++] = d;
    } while (n > 0);
    return i;
}

static int mq_str(uint8_t *b, int i, const String &s)
{
    b[i++] = s.length() >> 8;
    b[i++] = s.length() & 0xFF;
    memcpy(b + i, s.c_str(), s.length());
    return i + s.length();
}

static bool mq_read(Client *c, uint8_t *b, size_t n, uint32_t timeout)
{
    size_t got = 0;
    uint32_t t0 = millis();
    while (got < n && millis() - t0 < timeout) {
        if (cancelled()) return false;
        if (c->available()) {
            int r = c->read(b + got, n - got);
            if (r > 0) got += r;
        } else {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
    return got == n;
}

static int mq_read_len(Client *c)
{
    int mult = 1, val = 0;
    for (int k = 0; k < 4; k++) {
        uint8_t b;
        if (!mq_read(c, &b, 1, 2000)) return -1;
        val += (b & 127) * mult;
        if (!(b & 128)) return val;
        mult *= 128;
    }
    return -1;
}

static void tool_mqtt()
{
    String hp = cfg_get("mqtt", "");
    if (hp.length() == 0) {
        out_set("No broker set.\n\nOn the web config page, in Tools settings, add:\nmqtt=host:1883\nmquser=name\nmqpass=secret\nmqtopic=#");
        return;
    }
    String host = hp;
    uint16_t port = 1883;
    int colon = hp.indexOf(':');
    if (colon >= 0) { host = hp.substring(0, colon); port = (uint16_t)hp.substring(colon + 1).toInt(); }
    String user = cfg_get("mquser", "");
    String pass = cfg_get("mqpass", "");
    String topic = cfg_get("mqtopic", "#");
    if (user.length() > 60 || pass.length() > 60 || topic.length() > 100) { out_set("User, password or topic too long"); return; }

    WiFiClient plain;
    WiFiClientSecure sec;
    sec.setInsecure();
    bool tls = (port == 8883);
    Client *c = tls ? (Client *)&sec : (Client *)&plain;
    out_set("Connecting to\n" + host + ":" + String(port) + (tls ? " (TLS)" : "") + " ...");
    bool ok = tls ? (bool)sec.connect(host.c_str(), port) : (bool)plain.connect(host.c_str(), port, 5000);
    if (!ok) { out_set("Cannot connect to broker"); return; }

    uint8_t body[300], hdr[4];
    int i = 0;
    i = mq_str(body, i, "MQTT");
    body[i++] = 4;
    body[i++] = 0x02 | (user.length() ? 0x80 : 0) | (pass.length() ? 0x40 : 0);
    body[i++] = 0;
    body[i++] = 60;
    i = mq_str(body, i, String("pocketnoc-") + String((unsigned)(esp_random() & 0xFFFF), HEX));
    if (user.length()) i = mq_str(body, i, user);
    if (pass.length()) i = mq_str(body, i, pass);
    hdr[0] = 0x10;
    int hl = 1 + mq_len(hdr + 1, i);
    c->write(hdr, hl);
    c->write(body, i);

    uint8_t ack[4];
    if (!mq_read(c, ack, 4, 5000) || ack[0] != 0x20) { out_set("No answer from broker"); c->stop(); return; }
    if (ack[3] != 0) {
        out_set(fmt("Broker refused the login (code %d)\n%s", ack[3],
                    ack[3] == 4 || ack[3] == 5 ? "Check mquser and mqpass." : ""));
        c->stop();
        return;
    }

    i = 0;
    body[i++] = 0;
    body[i++] = 1;
    i = mq_str(body, i, topic);
    body[i++] = 0;
    hdr[0] = 0x82;
    hl = 1 + mq_len(hdr + 1, i);
    c->write(hdr, hl);
    c->write(body, i);

    String ring[8];
    int rn = 0;
    uint32_t msgs = 0, lastPing = millis(), lastDraw = 0;
    bool dirty = true;
    while (!cancelled() && c->connected()) {
        if (c->available()) {
            uint8_t h0;
            if (!mq_read(c, &h0, 1, 1000)) continue;
            int rl = mq_read_len(c);
            if (rl < 0) break;
            uint8_t pk[512];
            int keep = rl > 512 ? 512 : rl;
            if (!mq_read(c, pk, keep, 3000)) break;
            for (int k = keep; k < rl; k++) { uint8_t d; if (!mq_read(c, &d, 1, 2000)) break; }
            if ((h0 >> 4) == 3 && keep >= 2) {
                int tl = (pk[0] << 8) | pk[1];
                int qos = (h0 >> 1) & 3;
                int ps = 2 + tl + (qos ? 2 : 0);
                if (ps <= keep) {
                    String tp, pay;
                    for (int k = 0; k < tl && 2 + k < keep; k++) tp += (char)pk[2 + k];
                    for (int k = ps; k < keep && pay.length() < 80; k++) pay += (pk[k] >= 32 && pk[k] < 127) ? (char)pk[k] : '.';
                    ring[rn % 8] = tp + "\n  " + pay;
                    rn++;
                    msgs++;
                    dirty = true;
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (millis() - lastPing > 30000) {
            uint8_t pr[2] = {0xC0, 0x00};
            c->write(pr, 2);
            lastPing = millis();
        }
        if (dirty && millis() - lastDraw > 300) {
            String s = "Connected: " + host + ":" + String(port) + "\nTopic: " + topic + "\nMessages: " + String((unsigned long)msgs) + "\n\n";
            int show = rn < 8 ? rn : 8;
            for (int k = 0; k < show; k++) s += ring[(rn - 1 - k) % 8] + "\n";
            if (!rn) s += "Waiting for messages...";
            out_set(s);
            lastDraw = millis();
            dirty = false;
        }
    }
    uint8_t bye[2] = {0xE0, 0x00};
    c->write(bye, 2);
    c->stop();
    if (!cancelled()) out_set("Disconnected by broker");
}

#if NET_BLE
template <typename R> static int ble_count(R &r) { return r.getCount(); }
template <typename R> static int ble_count(R *r) { return r->getCount(); }
template <typename R> static BLEAdvertisedDevice ble_dev(R &r, int i) { return r.getDevice(i); }
template <typename R> static BLEAdvertisedDevice ble_dev(R *r, int i) { return r->getDevice(i); }
#endif

static void tool_ble()
{
#if NET_BLE
    out_set("Scanning Bluetooth...\n(about 6 seconds)");
    BLEDevice::init("");
    BLEScan *scan = BLEDevice::getScan();
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);
    auto res = scan->start(5, false);
    int n = ble_count(res);
    struct Dev { String name; String addr; int rssi; };
    Dev devs[16];
    int m = 0;
    for (int i = 0; i < n && m < 16; i++) {
        BLEAdvertisedDevice d = ble_dev(res, i);
        devs[m].name = String(d.getName().c_str());
        devs[m].addr = String(d.getAddress().toString().c_str());
        devs[m].rssi = d.getRSSI();
        m++;
    }
    for (int a = 0; a < m; a++)
        for (int b = a + 1; b < m; b++)
            if (devs[b].rssi > devs[a].rssi) { Dev t = devs[a]; devs[a] = devs[b]; devs[b] = t; }
    String out = String(n) + " devices (strongest first)\n\n";
    for (int i = 0; i < m; i++) {
        out += (devs[i].name.length() ? devs[i].name.substring(0, 16) : String("(no name)")) + "  " + String(devs[i].rssi) + " dBm\n  " + devs[i].addr + "\n";
    }
    scan->clearResults();
    BLEDevice::deinit(true);
    out_set(out);
#else
    out_set("Bluetooth scan is switched off.\nIn nettools.cpp set NET_BLE to 1.");
#endif
}

static void tool_qr()
{
#if LV_USE_QRCODE
    String url = "http://" + WiFi.localIP().toString() + "/";
    lvgl_port_lock(-1);
    lv_obj_t *qr = lv_qrcode_create(p_page, 150, lv_color_black(), lv_color_white());
    lv_qrcode_update(qr, url.c_str(), url.length());
    lv_obj_set_pos(qr, 40, 62);
    lv_obj_set_style_border_color(qr, lv_color_white(), 0);
    lv_obj_set_style_border_width(qr, 5, 0);
    p_qr = qr;
    lvgl_port_unlock();
    out_set("Scan with your phone to\nopen the config page:\n" + url);
    while (!cancelled()) vTaskDelay(pdMS_TO_TICKS(200));
    lvgl_port_lock(-1);
    if (p_qr == qr) { lv_obj_del(qr); p_qr = nullptr; }
    lvgl_port_unlock();
#else
    out_set("The QR widget is switched off.\nIn src/lv_conf.h set\n#define LV_USE_QRCODE 1");
#endif
}

// ================= the list of tools =================
// ---------- New devices: remember which MAC addresses are normal on this LAN ----------
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include <Preferences.h>

static const int NEWDEV_MAX = 40;

// MAC address from the ARP table, if this IP answered ARP recently.
static bool arp_mac(IPAddress ip, uint8_t *mac)
{
    ip4_addr_t a;
    IP4_ADDR(&a, ip[0], ip[1], ip[2], ip[3]);
    struct netif *n = netif_default;
    if (!n) return false;
    struct eth_addr *e = nullptr;
    const ip4_addr_t *ipr = nullptr;
    if (etharp_find_addr(n, &a, &e, &ipr) < 0 || !e) return false;
    memcpy(mac, e->addr, 6);
    return true;
}

static void tool_newdev()
{
    if (WiFi.status() != WL_CONNECTED) { out_set("Wi-Fi not connected"); return; }
    uint8_t known[NEWDEV_MAX * 6];
    int knownN = 0;
    {
        Preferences p;
        p.begin("newdev", true);
        size_t len = p.getBytesLength("macs");
        if (len > 0 && len <= sizeof(known) && len % 6 == 0) {
            p.getBytes("macs", known, len);
            knownN = (int)(len / 6);
        }
        p.end();
    }
    bool baseline = (knownN == 0);

    IPAddress me = WiFi.localIP();
    IPAddress gwip = WiFi.gatewayIP();
    uint8_t seen[NEWDEV_MAX * 6];
    int seenN = 0, found = 0, fresh = 0;
    String out, newOut;
    for (int h = 1; h <= 254 && !cancelled(); h++) {
        if (h == me[3]) continue;
        IPAddress ip(me[0], me[1], me[2], h);
        uint32_t rtt = 0;
        ping_once(ip, 150, rtt);           // the probe makes the board ask ARP "who has this address?"
        uint8_t mac[6];
        if (arp_mac(ip, mac)) {
            found++;
            bool isKnown = false;
            for (int k = 0; k < knownN; k++) if (memcmp(known + k * 6, mac, 6) == 0) { isKnown = true; break; }
            if (seenN < NEWDEV_MAX) { memcpy(seen + seenN * 6, mac, 6); seenN++; }
            char line[64];
            snprintf(line, sizeof(line), "%s  %02X:%02X:%02X:%02X:%02X:%02X",
                     ip.toString().c_str(), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            if (baseline || isKnown) {
                out += String(line) + (ip == gwip ? "  (router)" : "") + "\n";
            } else {
                fresh++;
                newOut += String("NEW  ") + line + "\n";
            }
        }
        if (h % 4 == 0 || h == 254) {
            out_set(fmt("Scanning %u.%u.%u.%d  (%d/254)\nFound: %d   New: %d\n\n", (unsigned)me[0], (unsigned)me[1],
                        (unsigned)me[2], h, h, found, fresh) + newOut + out);
        }
    }
    if (cancelled()) return;

    String head;
    if (baseline) {
        Preferences p;
        p.begin("newdev", false);
        p.putBytes("macs", seen, seenN * 6);
        p.end();
        head = fmt("Baseline saved: %d devices.\nNext scans flag anything not in it.\n\n", seenN);
    } else if (fresh) {
        head = fmt("%d NEW device(s)!\nStays flagged until you run\n'Forget devices' and scan again.\n\n", fresh);
    } else {
        head = fmt("No new devices (%d known).\n\n", found);
    }
    head += "Only devices that answer ARP show.\nA device that is off is not listed.\n(scans this /24 only)\n\n";
    out_set(head + newOut + out);
}

static void tool_forgetdev()
{
    Preferences p;
    p.begin("newdev", false);
    p.remove("macs");
    p.end();
    out_set("Known devices cleared.\nRun 'New devices' to learn the\ndevices on the network now.");
}

static const ToolDef TOOLS[] = {
    {"Wi-Fi", "Scan networks", tool_scan, 0, true,
     "Nearby access points with signal, channel and security. Warns about open networks and look-alike names."},
    {"Wi-Fi", "Channel map", tool_channels, 1, true,
     "How crowded each 2.4 GHz channel is, and the best of 1, 6 and 11 for your router."},
    {"Wi-Fi", "Signal meter", tool_signal, 1, true,
     "Live signal strength. Walk around with the device to find dead spots."},
    {"LAN", "Network info", tool_netinfo, 0, true,
     "Your address, router, DNS servers and access point."},
    {"LAN", "Ping", tool_ping, 1, false,
     "Ping with loss and jitter. Target is the router, or set target= in Tools settings. Tap Run."},
    {"LAN", "Find hosts", tool_hosts, 0, false,
     "Pings every address in your subnet. Takes up to a minute. Tap Run."},
    {"LAN", "Port check", tool_ports, 0, false,
     "Checks 16 common ports on the target host. Only use on hosts you own or manage. Tap Run."},
    {"LAN", "mDNS browser", tool_mdns, 0, false,
     "Lists devices that announce services (web, MQTT, SSH, printers). Takes about 18 seconds. Tap Run."},
    {"LAN", "New devices", tool_newdev, 0, false,
     "Scan the network and flag unknown MAC addresses"},
    {"LAN", "Forget devices", tool_forgetdev, 0, false,
     "Clear the known-devices list"},
    {"LAN", "Wake-on-LAN", tool_wol, 0, false,
     "Sends a magic packet to the MAC set as wol= in Tools settings. Tap Run."},
    {"Internet", "Latency test", tool_latency, 0, true,
     "Router, DNS and internet response times."},
    {"Internet", "Speed test", tool_speed, 0, false,
     "Rough download speed. Uses about 2 MB of data. Tap Run."},
    {"Internet", "DNS check", tool_dns, 0, false,
     "Times your router and public DNS servers, and detects DNS hijacking. Tap Run."},
    {"Internet", "Public IP", tool_publicip, 0, true,
     "Your address as the internet sees it, with the provider."},
    {"Internet", "HTTP timing", tool_http, 0, false,
     "Splits one request into DNS, TCP, TLS and wait time. Uses url= or your first service. Tap Run."},
    {"IoT", "MQTT inspector", tool_mqtt, 0, false,
     "Live messages from your broker. Set mqtt=, mquser=, mqpass= and mqtopic= in Tools settings. Tap Run."},
    {"IoT", "Bluetooth scan", tool_ble, 0, false,
     "Nearby Bluetooth LE devices with signal strength. Tap Run."},
    {"IoT", "Config QR", tool_qr, 2, true,
     "A QR code for this device's config page."},
};
static const int TOOL_COUNT = sizeof(TOOLS) / sizeof(TOOLS[0]);

// ================= tool task and page =================
static void tool_task(void *arg)
{
    int idx = (int)(intptr_t)arg;
    t_gen = s_gen;
    bool needsNet = strcmp(TOOLS[idx].group, "Wi-Fi") != 0 && strcmp(TOOLS[idx].name, "Bluetooth scan") != 0;
    if (needsNet && WiFi.status() != WL_CONNECTED) {
        out_set("Wi-Fi is not connected.\n\nThis tool needs a network.\nCheck the Info page, or the\nserial monitor for the reason.");
    } else {
        TOOLS[idx].run();
    }
    lvgl_port_lock(-1);
    lv_label_set_text(p_run_lbl, "Run");
    lvgl_port_unlock();
    s_busy = false;
    vTaskDelete(nullptr);
}

static void start_tool()
{
    s_busy = true;
    s_cancel = false;
    lv_label_set_text(p_run_lbl, "Stop");
    xTaskCreatePinnedToCore(tool_task, "nettool", 12288, (void *)(intptr_t)cur, 1, nullptr, 0);
}

static void on_run_click(lv_event_t *e)
{
    if (s_busy) { s_cancel = true; return; }
    start_tool();
}

static void on_back_click(lv_event_t *e)
{
    s_cancel = true;
    s_gen++;
    if (p_qr) { lv_obj_del(p_qr); p_qr = nullptr; }
    lv_obj_add_flag(p_page, LV_OBJ_FLAG_HIDDEN);
}

static void open_tool(int idx)
{
    cur = idx;
    s_gen++;
    if (p_qr) { lv_obj_del(p_qr); p_qr = nullptr; }
    const ToolDef &d = TOOLS[idx];
    lv_label_set_text(p_title, d.name);
    int y = 62;
    if (d.visual == 1) {
        lv_chart_set_all_value(p_chart, p_ser, LV_CHART_POINT_NONE);
        lv_obj_clear_flag(p_chart, LV_OBJ_FLAG_HIDDEN);
        y = 138;
    } else {
        lv_obj_add_flag(p_chart, LV_OBJ_FLAG_HIDDEN);
        if (d.visual == 2) y = 222;
    }
    lv_obj_set_pos(p_box, 6, y);
    lv_obj_set_size(p_box, 228, 268 - y);
    lv_label_set_text(p_out, d.hint);
    lv_obj_scroll_to_y(p_box, 0, LV_ANIM_OFF);
    lv_label_set_text(p_run_lbl, s_busy ? "Stop" : "Run");
    lv_obj_clear_flag(p_page, LV_OBJ_FLAG_HIDDEN);
    if (d.autorun && !s_busy) start_tool();
}

static void on_menu_click(lv_event_t *e)
{
    open_tool((int)(intptr_t)lv_event_get_user_data(e));
}

void nettools_build_menu(lv_obj_t *tile)
{
    lv_obj_t *title = lv_label_create(tile);
    lv_label_set_text(title, "Tools");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_30, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t *cont = lv_obj_create(tile);
    lv_obj_set_size(cont, 228, 232);
    lv_obj_align(cont, LV_ALIGN_TOP_LEFT, 6, 44);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 2, 0);
    lv_obj_set_style_pad_row(cont, 6, 0);
    lv_obj_set_style_text_color(cont, NT_TEXT, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);

    const char *last = "";
    for (int i = 0; i < TOOL_COUNT; i++) {
        if (strcmp(last, TOOLS[i].group) != 0) {
            last = TOOLS[i].group;
            lv_obj_t *h = lv_label_create(cont);
            lv_label_set_text(h, last);
            lv_obj_set_style_text_font(h, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(h, lv_palette_main(LV_PALETTE_LIGHT_BLUE), 0);
        }
        lv_obj_t *b = lv_btn_create(cont);
        lv_obj_set_size(b, LV_PCT(100), 34);
        lv_obj_add_event_cb(b, on_menu_click, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, TOOLS[i].name);
        lv_obj_center(l);
    }
}

void nettools_build_page(lv_obj_t *screen)
{
    p_page = lv_obj_create(screen);
    lv_obj_set_size(p_page, 240, 320);
    lv_obj_set_pos(p_page, 0, 0);
    lv_obj_set_style_radius(p_page, 0, 0);
    lv_obj_set_style_border_width(p_page, 0, 0);
    lv_obj_set_style_pad_all(p_page, 0, 0);
    lv_obj_set_style_bg_color(p_page, NT_BG, 0);
    lv_obj_set_style_bg_opa(p_page, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(p_page, NT_TEXT, 0);
    lv_obj_clear_flag(p_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p_page, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *back = lv_btn_create(p_page);
    lv_obj_set_size(back, 84, 30);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 6, 4);
    lv_obj_add_event_cb(back, on_back_click, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, "< Back");
    lv_obj_center(bl);

    lv_obj_t *run = lv_btn_create(p_page);
    lv_obj_set_size(run, 84, 30);
    lv_obj_align(run, LV_ALIGN_TOP_RIGHT, -6, 4);
    lv_obj_add_event_cb(run, on_run_click, LV_EVENT_CLICKED, nullptr);
    p_run_lbl = lv_label_create(run);
    lv_label_set_text(p_run_lbl, "Run");
    lv_obj_center(p_run_lbl);

    p_title = lv_label_create(p_page);
    lv_obj_set_style_text_font(p_title, &lv_font_montserrat_16, 0);
    lv_label_set_long_mode(p_title, LV_LABEL_LONG_DOT);
    lv_obj_set_size(p_title, 228, 20);
    lv_obj_align(p_title, LV_ALIGN_TOP_LEFT, 6, 38);
    lv_label_set_text(p_title, "");

    p_chart = lv_chart_create(p_page);
    lv_obj_set_size(p_chart, 228, 70);
    lv_obj_align(p_chart, LV_ALIGN_TOP_LEFT, 6, 62);
    lv_obj_set_style_bg_color(p_chart, NT_CHART_BG, 0);
    lv_obj_set_style_border_width(p_chart, 0, 0);
    lv_obj_set_style_line_color(p_chart, NT_GRID, LV_PART_MAIN);
    lv_obj_set_style_size(p_chart, 0, LV_PART_INDICATOR);
    lv_chart_set_div_line_count(p_chart, 3, 0);
    p_ser = lv_chart_add_series(p_chart, lv_palette_main(LV_PALETTE_LIGHT_BLUE), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_add_flag(p_chart, LV_OBJ_FLAG_HIDDEN);

    p_box = lv_obj_create(p_page);
    lv_obj_set_size(p_box, 228, 206);
    lv_obj_set_pos(p_box, 6, 62);
    lv_obj_set_style_bg_opa(p_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p_box, 0, 0);
    lv_obj_set_style_pad_all(p_box, 2, 0);
    lv_obj_set_scroll_dir(p_box, LV_DIR_VER);

    p_out = lv_label_create(p_box);
    lv_obj_set_width(p_out, 216);
    lv_obj_set_style_text_line_space(p_out, 3, 0);
    lv_label_set_text(p_out, "");
}

void nettools_open(const char *name)
{
    for (int i = 0; i < TOOL_COUNT; i++) {
        if (strcmp(TOOLS[i].name, name) == 0) { open_tool(i); return; }
    }
}
void nettools_close() { on_back_click(nullptr); }
bool nettools_is_open() { return p_page && !lv_obj_has_flag(p_page, LV_OBJ_FLAG_HIDDEN); }
bool nettools_is_busy() { return s_busy; }

// Used by main.cpp for ping:// service checks. Returns 1 = reply, 0 = no reply, -1 = a tool is using the ICMP helper.
int nettools_ping(const char *host, uint32_t timeout_ms, uint32_t &rtt)
{
    if (s_busy) return -1;
    IPAddress ip;
    if (!resolve(String(host), ip)) return 0;
    return ping_once(ip, timeout_ms, rtt) ? 1 : 0;
}
