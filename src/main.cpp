#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFiMulti.h>
#include <Preferences.h>
#include <base64.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"
#include "nettools.h"
#include "secrets.h"

#ifndef CONFIG_PASSWORD
#error "Add  #define CONFIG_PASSWORD \"...\"  to src/secrets.h"
#endif

#include <ArduinoOTA.h>
#include "ota_password.h"
static bool otaStarted = false;
#include <WiFiUdp.h>
using namespace esp_panel::drivers;
using namespace esp_panel::board;

// ---- Settings ----
static const int MAX_SERVICES = 5;
static const uint32_t CHECK_INTERVAL_MS = 20000;
static const int FAIL_THRESHOLD = 2;   // consecutive failures before a service counts as down
#define BUZZER_PIN -1                  // set to the real GPIO once we find it (-1 = disabled)
#define BUZZER_PASSIVE 1               // 1 = passive buzzer (tone), 0 = active buzzer (on/off)

static const char *DEFAULT_CFG =
    "GitHub, https://github.com\n"
    "GitHub API, https://api.github.com\n"
    "Example, https://example.com\n"
    "Broken test, https://github.com/this-page-does-not-exist-404\n";

// ---- Service configuration (shared between tasks, guarded by cfgMutex) ----
struct Service { char name[24]; char url[110]; char word[24]; uint16_t every; uint16_t slow; };
static Service cfg[MAX_SERVICES];
static int cfgCount = 0;
static volatile bool cfgChanged = true;
static SemaphoreHandle_t cfgMutex;
static String raw_cfg;
static String raw_tools;   // "key=value" lines for the network tools
static Preferences prefs;
static WebServer server(80);
static bool serverStarted = false;

// ---- Monitoring state ----
static int fails[MAX_SERVICES];
static bool confirmedDown[MAX_SERVICES];
static volatile bool acked[MAX_SERVICES];
static volatile bool alarmActive = false;

// ---- UI objects ----
static lv_obj_t *dots[MAX_SERVICES];
static lv_obj_t *names[MAX_SERVICES];
static lv_obj_t *infos[MAX_SERVICES];
static lv_obj_t *ip_label = nullptr;
static lv_obj_t *summary = nullptr;
static lv_obj_t *banner = nullptr;
static lv_obj_t *banner_label = nullptr;

// ---- Phone alert settings: an ntfytopic= line in the Tools settings ----
static char ntTopic[65];               // guarded by cfgMutex
static volatile bool ntTest = false;   // set when a new topic is saved: send one test message

static String tools_get(const String &text, const char *key)
{
    String k = String(key) + "=";
    int pos = 0, len = text.length();
    while (pos < len) {
        int eol = text.indexOf('\n', pos);
        if (eol < 0) eol = len;
        String line = text.substring(pos, eol);
        pos = eol + 1;
        line.trim();
        if (line.startsWith(k)) {
            String v = line.substring(k.length());
            v.trim();
            return v;
        }
    }
    return "";
}

static bool topic_ok(const String &t)
{
    if (t.length() < 1 || t.length() > 64) return false;
    for (int i = 0; i < t.length(); i++) {
        char c = t.c_str()[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
}

static void notify_reload(bool announce)
{
    String t = tools_get(raw_tools, "ntfytopic");
    if (t.length() && !topic_ok(t)) {
        Serial.println("notify: ntfytopic may only use letters, digits, - and _ (max 64 characters); ignoring it");
        t = "";
    }
    bool changed;
    xSemaphoreTake(cfgMutex, portMAX_DELAY);
    changed = strcmp(ntTopic, t.c_str()) != 0;
    strlcpy(ntTopic, t.c_str(), sizeof(ntTopic));
    xSemaphoreGive(cfgMutex);
    if (announce && changed && t.length()) ntTest = true;
}

// ================= Config parsing / storage =================
// Format: one service per line:  "Name, https://url"  "Name, tcp://host:port"  "Name, ping://host"
// A web line may end with ", contains=word". Returns the number parsed.
static bool url_ok(const String &u)
{
    return u.startsWith("http://") || u.startsWith("https://") || u.startsWith("tcp://") || u.startsWith("ping://");
}

static int parse_config(const String &text, Service *out)
{
    int n = 0;
    int pos = 0;
    int len = text.length();
    while (pos < len && n < MAX_SERVICES) {
        int eol = text.indexOf('\n', pos);
        if (eol < 0) eol = len;
        String line = text.substring(pos, eol);
        pos = eol + 1;
        line.trim();
        if (line.length() == 0) continue;
        int comma = line.indexOf(',');
        if (comma < 1) continue;
        String name = line.substring(0, comma);
        String url = line.substring(comma + 1);
        String word = "";
        int every = 0, slow = 0;
        int c2;
        while ((c2 = url.lastIndexOf(',')) >= 0) {   // options at the end of the line, in any order
            String tail = url.substring(c2 + 1);
            tail.trim();
            if (tail.startsWith("contains=")) { word = tail.substring(9); word.trim(); }
            else if (tail.startsWith("every=")) every = tail.substring(6).toInt();
            else if (tail.startsWith("slow=")) slow = tail.substring(5).toInt();
            else break;
            url = url.substring(0, c2);
        }
        if (every > 0 && every < 10) every = 10;
        if (every > 3600) every = 3600;
        if (slow < 0) slow = 0;
        if (slow > 60000) slow = 60000;
        name.trim();
        url.trim();
        if (name.length() == 0 || !url_ok(url)) continue;
        if (!(url.startsWith("http://") || url.startsWith("https://"))) word = "";   // keywords: web checks only
        strlcpy(out[n].name, name.c_str(), sizeof(out[n].name));
        strlcpy(out[n].url, url.c_str(), sizeof(out[n].url));
        strlcpy(out[n].word, word.c_str(), sizeof(out[n].word));
        out[n].every = (uint16_t)every;
        out[n].slow = (uint16_t)slow;
        n++;
    }
    return n;
}

static void load_config()
{
    prefs.begin("statusboard", false);
    raw_cfg = prefs.getString("cfg", DEFAULT_CFG);
    raw_tools = prefs.isKey("tools") ? prefs.getString("tools", "") : String("");
    prefs.end();
    nettools_set_config(raw_tools);
    notify_reload(false);

    Service tmp[MAX_SERVICES];
    int n = parse_config(raw_cfg, tmp);
    if (n > 0) nettools_set_default_url(tmp[0].url);
    xSemaphoreTake(cfgMutex, portMAX_DELAY);
    memcpy(cfg, tmp, sizeof(cfg));
    cfgCount = n;
    xSemaphoreGive(cfgMutex);
    cfgChanged = true;
}

static void save_config(const String &text, const Service *parsed, int n)
{
    prefs.begin("statusboard", false);
    prefs.putString("cfg", text);
    prefs.end();
    raw_cfg = text;

    xSemaphoreTake(cfgMutex, portMAX_DELAY);
    memcpy(cfg, parsed, sizeof(Service) * MAX_SERVICES);
    cfgCount = n;
    xSemaphoreGive(cfgMutex);
    cfgChanged = true;
}

// ================= Web config page =================
static String html_escape(const String &s)
{
    String o;
    o.reserve(s.length() + 16);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '&') o += "&amp;";
        else if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '"') o += "&quot;";
        else o += c;
    }
    return o;
}

// ---- Login: password form + session cookie (Basic auth still accepted for curl) ----
static String sessionToken = "";
static uint32_t lockUntil = 0;
static int badTries = 0;

static String make_token()
{
    char buf[33];
    for (int i = 0; i < 4; i++) snprintf(buf + i * 8, 9, "%08x", (unsigned)esp_random());
    return String(buf);
}

static bool has_session()
{
    if (sessionToken.length() == 0) return false;
    return server.header("Cookie").indexOf("sid=" + sessionToken) >= 0;
}

static bool basic_ok()
{
    String expected = "Basic " + base64::encode(String("admin:") + CONFIG_PASSWORD);
    return server.header("Authorization") == expected;
}

static bool authed()
{
    if (has_session() || basic_ok()) return true;
    Serial.println("auth: not logged in, sending to /login");
    server.sendHeader("Location", "/login");
    server.send(303);
    return false;
}

static void send_login_page(int status, const char *msg)
{
    String page = F("<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
                    "<title>" DEVICE_NAME " login</title></head>"
                    "<body style='font-family:sans-serif;max-width:360px;margin:40px auto;padding:0 12px'>"
                    "<h2>" DEVICE_NAME "</h2>");
    if (msg[0]) {
        page += "<p style='color:#b00'>";
        page += msg;
        page += "</p>";
    }
    page += F("<form method=POST action=/login>"
              "<input type=password name=password autofocus autocomplete=current-password "
              "placeholder=Password style='width:100%;padding:8px;box-sizing:border-box'><br>"
              "<button type=submit style='margin-top:8px;padding:8px 16px'>Log in</button>"
              "</form><p><a href=/logout>Log out</a></p></body></html>");
    server.send(status, "text/html", page);
}

static void handle_login_page()
{
    send_login_page(200, "");
}

static void handle_login()
{
    uint32_t now = millis();
    if (lockUntil != 0 && (int32_t)(lockUntil - now) > 0) {
        send_login_page(429, "Too many attempts. Wait a minute and try again.");
        return;
    }
    String pw = server.arg("password");
    if (pw == CONFIG_PASSWORD) {
        badTries = 0;
        sessionToken = make_token();
        server.sendHeader("Set-Cookie", "sid=" + sessionToken + "; Path=/; HttpOnly; SameSite=Strict");
        server.sendHeader("Location", "/");
        server.send(303);
        Serial.println("login: success");
    } else {
        Serial.printf("login: wrong password (got %d chars, expected %d)\n",
                      (int)pw.length(), (int)strlen(CONFIG_PASSWORD));
        if (++badTries >= 5) {
            lockUntil = now + 60000;
            badTries = 0;
            if (lockUntil == 0) lockUntil = 1;
        }
        delay(500);
        send_login_page(401, "Wrong password.");
    }
}

static void handle_logout()
{
    sessionToken = "";
    server.sendHeader("Set-Cookie", "sid=; Path=/; Max-Age=0");
    server.sendHeader("Location", "/login");
    server.send(303);
}


static void handle_root()
{
    if (!authed()) return;
    String page = F("<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
                    "<title>" DEVICE_NAME "</title></head>"
                    "<body style='font-family:sans-serif;max-width:520px;margin:20px auto;padding:0 12px'>"
                    "<h2>" DEVICE_NAME "</h2>"
                    "<form method=POST action=/save>"
                    "<h3>Services to monitor</h3>"
                    "<p>One service per line (max 5): <code>Name, https://url</code>, or <code>Name, tcp://host:port</code> (port check), or <code>Name, ping://host</code>. Add <code>, contains=word</code> to a web line to require that word in the page (case sensitive, up to 23 characters, searched in the first 16 KB). A line can also end with <code>, every=60</code> (check at most every 60 seconds, minimum 10) and <code>, slow=1500</code> (alert when 3 checks in a row take longer than 1500 ms).</p>"
                    "<textarea name=cfg rows=6 style='width:100%;font-family:monospace'>");
    page += html_escape(raw_cfg);
    page += F("</textarea>"
              "<h3>Tools settings (optional)</h3>"
              "<p>One <code>key=value</code> per line. Keys: <code>target</code> (host for ping and port check), "
              "<code>url</code> (for HTTP timing), <code>wol</code> (MAC address to wake), "
              "<code>mqtt</code> (host:port), <code>mquser</code>, <code>mqpass</code>, <code>mqtopic</code>, "
              "<code>speedurl</code>, <code>ntfytopic</code> (phone alerts through ntfy.sh: anyone who knows the topic name can read your alerts, so use a long random one). Note: the MQTT password is stored on the device as plain text.</p>"
              "<textarea name=tools rows=6 style='width:100%;font-family:monospace'>");
    page += html_escape(raw_tools);
    page += F("</textarea><br><button type=submit style='margin-top:8px;padding:8px 16px'>Save</button>"
              "</form><p><a href=/wifi>Wi-Fi networks</a> | <a href=/tools>More tools</a> | <a href=/logout>Log out</a></p></body></html>");
    server.send(200, "text/html", page);
}

static void handle_save()
{
    if (!authed()) return;
    String text = server.arg("cfg");
    if (text.length() > 800) {
        server.send(400, "text/plain", "Too long (max 800 characters)");
        return;
    }
    Service parsed[MAX_SERVICES];
    memset(parsed, 0, sizeof(parsed));
    int n = parse_config(text, parsed);
    if (n == 0) {
        server.send(400, "text/plain", "No valid lines. Use:  Name, https://url");
        return;
    }
    if (server.hasArg("tools")) {
        String tools = server.arg("tools");
        if (tools.length() > 600) {
            server.send(400, "text/plain", "Tools settings too long (max 600 characters)");
            return;
        }
        prefs.begin("statusboard", false);
        prefs.putString("tools", tools);
        prefs.end();
        raw_tools = tools;
        nettools_set_config(raw_tools);
        notify_reload(true);
    }
    save_config(text, parsed, n);
    nettools_set_default_url(parsed[0].url);
    Serial.printf("Config saved: %d services\n", n);
    server.sendHeader("Location", "/");
    server.send(303);
}

// ================= Wi-Fi: several saved networks + setup access point =================
static WiFiMulti wifiMulti;
static String raw_wifi;          // extra networks saved on the web page, one "name, password" per line
static bool apActive = false;
static uint32_t lastWifiTry = 0, wifiDownSince = 0;

static void wifi_add_networks()
{
    wifiMulti.APlistClean();
    wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);                 // the one in secrets.h
    prefs.begin("statusboard", false);
    raw_wifi = prefs.isKey("wifi") ? prefs.getString("wifi", "") : String("");
    prefs.end();
    int pos = 0, len = raw_wifi.length(), added = 0;
    while (pos < len && added < 5) {
        int eol = raw_wifi.indexOf('\n', pos);
        if (eol < 0) eol = len;
        String line = raw_wifi.substring(pos, eol);
        pos = eol + 1;
        line.trim();
        if (line.length() == 0) continue;
        int comma = line.indexOf(',');
        String ssid = comma < 0 ? line : line.substring(0, comma);
        String pw = comma < 0 ? String("") : line.substring(comma + 1);
        ssid.trim();
        pw.trim();
        if (ssid.length() == 0) continue;
        wifiMulti.addAP(ssid.c_str(), pw.length() ? pw.c_str() : nullptr);
        added++;
    }
    Serial.printf("Wi-Fi: %d saved networks (+ secrets.h)\n", added);
}

static void wifi_service()
{
    uint32_t now = millis();
    if (WiFi.status() == WL_CONNECTED) {
        wifiDownSince = 0;
        if (apActive) {
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            apActive = false;
            Serial.println("Wi-Fi connected, setup mode off");
        }
        return;
    }
    if (wifiDownSince == 0) wifiDownSince = now;
    bool someoneSettingUp = apActive && WiFi.softAPgetStationNum() > 0;
    uint32_t interval = apActive ? 30000 : 8000;
    if (!someoneSettingUp && now - lastWifiTry > interval) {
        lastWifiTry = now;
        wifiMulti.run(6000);                                   // joins the strongest saved network in range
    }
    if (!apActive && now - wifiDownSince > 45000) {
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(DEVICE_NAME "-Setup");
        apActive = true;
        Serial.println("Wi-Fi setup mode: join '" DEVICE_NAME "-Setup' and open http://192.168.4.1/");
    }
}

static void handle_wifi_page()
{
    if (!authed()) return;
    String page = F("<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
                    "<title>" DEVICE_NAME " Wi-Fi</title></head>"
                    "<body style='font-family:sans-serif;max-width:520px;margin:20px auto;padding:0 12px'>"
                    "<h2>Wi-Fi networks</h2>"
                    "<p>One per line: <code>Network name, password</code> (leave the password out for an open network, "
                    "max 5). The board joins the strongest saved network in range. The network in secrets.h is always "
                    "tried too. Saving restarts the board.</p>"
                    "<form method=POST action=/wifi>"
                    "<textarea name=nets rows=6 style='width:100%;font-family:monospace'>");
    page += html_escape(raw_wifi);
    page += F("</textarea><br><button type=submit style='margin-top:8px;padding:8px 16px'>Save and restart</button>"
              "</form><p><a href=/>Back</a> | <a href=/tools>More tools</a></p></body></html>");
    server.send(200, "text/html", page);
}

static void handle_wifi_save()
{
    if (!authed()) return;
    String text = server.arg("nets");
    if (text.length() > 600) {
        server.send(400, "text/plain", "Too long (max 600 characters)");
        return;
    }
    int pos = 0, len = text.length(), n = 0;
    while (pos < len) {
        int eol = text.indexOf('\n', pos);
        if (eol < 0) eol = len;
        String line = text.substring(pos, eol);
        pos = eol + 1;
        line.trim();
        if (line.length() == 0) continue;
        n++;
        int comma = line.indexOf(',');
        String ssid = comma < 0 ? line : line.substring(0, comma);
        String pw = comma < 0 ? String("") : line.substring(comma + 1);
        ssid.trim();
        pw.trim();
        if (ssid.length() < 1 || ssid.length() > 32) {
            server.send(400, "text/plain", "Network names must be 1 to 32 characters (line " + String(n) + ")");
            return;
        }
        if (pw.length() != 0 && (pw.length() < 8 || pw.length() > 63)) {
            server.send(400, "text/plain", "Passwords must be 8 to 63 characters (line " + String(n) + ")");
            return;
        }
    }
    if (n > 5) {
        server.send(400, "text/plain", "Max 5 networks");
        return;
    }
    prefs.begin("statusboard", false);
    prefs.putString("wifi", text);
    prefs.end();
    raw_wifi = text;
    server.send(200, "text/html", "<!doctype html><body style='font-family:sans-serif;padding:20px'>Saved. The board is restarting...</body>");
    delay(500);
    ESP.restart();
}

#include "incidents.h"

// ================= UI =================
// Navigation: swipe sideways or use the tab bar.
// Pages: 0 Home, 1 Status, 2 Tools, 3 Clock, 4 Info.
// Tapping a service opens its detail page on top (full screen, < Back).
// LVGL callbacks and *_nolock functions run with the LVGL lock already held.
// ---- Saved UI settings (Settings page; kept in flash, survive reboots) ----
struct UiSettings {
    bool darkTheme = true;
    bool alertsOn = true;       // false: no red banner and no buzzer (incidents are still logged)
    bool buzzerMute = false;
    uint16_t autoReturnS = 90;  // seconds without a touch before going back to Home, 0 = never
    bool clock24h = true;
};
static UiSettings ui_set;
bool ui_dark = true;            // not static: nettools.cpp reads it too
static lv_obj_t *spage = nullptr;
static lv_obj_t *s_return_lbl = nullptr;
static lv_obj_t *s_clock_lbl = nullptr;

static void ui_settings_load()
{
    Preferences p;
    p.begin("uiset", false);
    ui_set.darkTheme = p.getBool("dark", true);
    ui_set.alertsOn = p.getBool("alerts", true);
    ui_set.buzzerMute = p.getBool("mute", false);
    ui_set.autoReturnS = p.getUShort("ret", 90);
    ui_set.clock24h = p.getBool("h24", true);
    p.end();
    ui_dark = ui_set.darkTheme;
}

static void ui_settings_save()
{
    Preferences p;
    p.begin("uiset", false);
    p.putBool("dark", ui_set.darkTheme);
    p.putBool("alerts", ui_set.alertsOn);
    p.putBool("mute", ui_set.buzzerMute);
    p.putUShort("ret", ui_set.autoReturnS);
    p.putBool("h24", ui_set.clock24h);
    p.end();
}

// Header clock text: "14:05" or "2:05p"
static void fmt_hm(char *buf, size_t n, const struct tm &ti)
{
    if (ui_set.clock24h) {
        snprintf(buf, n, "%02d:%02d", ti.tm_hour, ti.tm_min);
    } else {
        int h = ti.tm_hour % 12;
        if (h == 0) h = 12;
        snprintf(buf, n, "%d:%02d%c", h, ti.tm_min, ti.tm_hour < 12 ? 'a' : 'p');
    }
}
#define CLOCK_TZ "EAT-3"          // POSIX time zone: EAT-3 means UTC+3. Change for another zone.
static const int TAB_COUNT = 5;

// Colours follow the saved theme (ui_dark). Changing the theme restarts the board.
#define C_BG       (ui_dark ? lv_color_hex(0x0f141a) : lv_color_hex(0xffffff))
#define C_TEXT     (ui_dark ? lv_color_hex(0xe6ebf0) : lv_color_hex(0x1c2530))
#define C_CARD     (ui_dark ? lv_color_hex(0x232c37) : lv_color_hex(0xe4e8ec))
#define C_CHART_BG (ui_dark ? lv_color_hex(0x1a222b) : lv_color_hex(0xf2f4f6))
#define C_GRID     (ui_dark ? lv_color_hex(0x364352) : lv_color_hex(0xd0d6dc))

static const char *TAB_TEXT[TAB_COUNT] = {
    LV_SYMBOL_HOME "\nHome", LV_SYMBOL_LIST "\nStatus", LV_SYMBOL_SETTINGS "\nTools",
    LV_SYMBOL_BELL "\nClock", LV_SYMBOL_EYE_OPEN "\nInfo"
};
static const char *TAB_ICON[TAB_COUNT] = {
    LV_SYMBOL_HOME, LV_SYMBOL_LIST, LV_SYMBOL_SETTINGS, LV_SYMBOL_BELL, LV_SYMBOL_EYE_OPEN
};

static const int HIST_LEN = 20;
struct History {
    int16_t ms[HIST_LEN];   // response time in ms, -1 = failed check
    int head, n;
    uint32_t checks, upChecks;
    uint32_t lastMs;
    int lastCode;
    bool has, lastOk;
};
static History hist[MAX_SERVICES];
static Service uiSvc[MAX_SERVICES];
static int uiCount = 0;
static volatile int selectedSvc = -1;
static volatile bool checkNow = false;     // set by the Home "Check" button, read by the monitor task

static lv_obj_t *tv = nullptr;
static lv_obj_t *rows[MAX_SERVICES];
static lv_obj_t *tab_lbl[TAB_COUNT];
static lv_obj_t *dpage = nullptr;
static lv_obj_t *d_name = nullptr;
static lv_obj_t *d_status = nullptr;
static lv_obj_t *d_url = nullptr;
static lv_obj_t *d_stats = nullptr;
static lv_obj_t *d_chart = nullptr;
static lv_chart_series_t *d_series = nullptr;
static lv_obj_t *info_label = nullptr;
static lv_obj_t *c_time = nullptr;
static lv_obj_t *c_date = nullptr;
static lv_obj_t *c_hand_h = nullptr;
static lv_obj_t *c_hand_m = nullptr;
static lv_obj_t *c_hand_s = nullptr;
static lv_point_t pts_h[2], pts_m[2], pts_s[2];
static lv_obj_t *h_arc = nullptr;
static lv_obj_t *h_count = nullptr;
static lv_obj_t *h_caption = nullptr;
static lv_obj_t *h_time = nullptr;
static lv_obj_t *h_wifi = nullptr;
static lv_obj_t *h_avg = nullptr;
static lv_obj_t *h_sig = nullptr;
static lv_obj_t *h_up = nullptr;
static lv_obj_t *hpage = nullptr;
static lv_obj_t *hist_box = nullptr;
static lv_obj_t *hist_label = nullptr;
static uint32_t hist_shown = 0;

static void paint(lv_obj_t *o)
{
    lv_obj_set_style_text_color(o, C_TEXT, 0);
}

// ---- Service detail page ----
static void render_detail_nolock()
{
    int i = selectedSvc;
    if (i < 0 || i >= uiCount) {
        lv_label_set_text(d_name, "No service selected");
        lv_label_set_text(d_status, "-");
        lv_label_set_text(d_url, "");
        lv_label_set_text(d_stats, "");
        lv_chart_set_all_value(d_chart, d_series, LV_CHART_POINT_NONE);
        lv_chart_refresh(d_chart);
        return;
    }
    const History &h = hist[i];
    lv_label_set_text(d_name, uiSvc[i].name);
    lv_label_set_text(d_url, uiSvc[i].url);

    if (!h.has) {
        lv_label_set_text(d_status, "...");
        lv_obj_set_style_text_color(d_status, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_label_set_text(d_stats, "No checks yet");
        lv_chart_set_all_value(d_chart, d_series, LV_CHART_POINT_NONE);
        lv_chart_refresh(d_chart);
        return;
    }

    lv_label_set_text(d_status, h.lastOk ? "UP" : "DOWN");
    lv_obj_set_style_text_color(d_status,
        lv_palette_main(h.lastOk ? LV_PALETTE_GREEN : LV_PALETTE_RED), 0);

    int start = (h.head - h.n + HIST_LEN) % HIST_LEN;
    long sum = 0;
    int okc = 0, maxv = 0;
    for (int k = 0; k < h.n; k++) {
        int v = h.ms[(start + k) % HIST_LEN];
        if (v >= 0) {
            sum += v;
            okc++;
            if (v > maxv) maxv = v;
        }
    }
    int avg = okc ? (int)(sum / okc) : 0;
    int pct = h.checks ? (int)((h.upChecks * 100UL) / h.checks) : 0;
    lv_label_set_text_fmt(d_stats,
        "Last: %lu ms (code %d)\nAverage: %d ms (last %d)\nUptime: %d%% (%lu of %lu)",
        (unsigned long)h.lastMs, h.lastCode, avg, h.n,
        pct, (unsigned long)h.upChecks, (unsigned long)h.checks);

    int ymax = ((maxv + 499) / 500) * 500;
    if (ymax < 500) ymax = 500;
    lv_chart_set_range(d_chart, LV_CHART_AXIS_PRIMARY_Y, 0, ymax);
    for (int pos = 0; pos < HIST_LEN; pos++) {
        int j = pos - (HIST_LEN - h.n);
        lv_coord_t v = LV_CHART_POINT_NONE;
        if (j >= 0) {
            int m = h.ms[(start + j) % HIST_LEN];
            if (m >= 0) v = m;
        }
        lv_chart_set_value_by_id(d_chart, d_series, pos, v);
    }
    lv_chart_refresh(d_chart);
}

// ---- Info page ----
static void update_info_nolock()
{
    if (!info_label) return;
    if (WiFi.status() != WL_CONNECTED) {
        if (apActive) {
            lv_label_set_text(info_label, "Wi-Fi setup mode\n\n1. Join the Wi-Fi\n    " DEVICE_NAME "-Setup\n2. Open 192.168.4.1\n3. Log in, then tap\n    Wi-Fi networks");
        } else {
            lv_label_set_text_fmt(info_label, "Wi-Fi: connecting...\nUptime: %lu min\nServices: %d",
                                  (unsigned long)(millis() / 60000UL), uiCount);
        }
        return;
    }
    String ssid = WiFi.SSID();
    String ip = WiFi.localIP().toString();
    lv_label_set_text_fmt(info_label,
        "Wi-Fi: %s\nSignal: %d dBm\nIP: %s\nConfig: http://%s/\nHeap free: %u KB\nUptime: %lu min\nServices: %d\nBuild: " __DATE__,
        ssid.c_str(), (int)WiFi.RSSI(), ip.c_str(), ip.c_str(),
        (unsigned)(ESP.getFreeHeap() / 1024), (unsigned long)(millis() / 60000UL), uiCount);
}

static void on_info_timer(lv_timer_t *t)
{
    update_info_nolock();
}

// ---- Clock page ----
static void set_hand(lv_obj_t *hand, lv_point_t *pts, float deg, float len)
{
    float r = deg * 0.0174533f;
    pts[0].x = 100;
    pts[0].y = 100;
    pts[1].x = (lv_coord_t)(100 + len * sinf(r));
    pts[1].y = (lv_coord_t)(100 - len * cosf(r));
    lv_line_set_points(hand, pts, 2);
}

static void on_clock_timer(lv_timer_t *t)
{
    struct tm ti;
    if (!getLocalTime(&ti, 0)) {
        lv_label_set_text(c_time, "--:--:--");
        lv_label_set_text(c_date, "Syncing time...");
        return;
    }
    char buf[32];
    if (ui_set.clock24h) {
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        int h12 = ti.tm_hour % 12;
        if (h12 == 0) h12 = 12;
        snprintf(buf, sizeof(buf), "%d:%02d:%02d %s", h12, ti.tm_min, ti.tm_sec, ti.tm_hour < 12 ? "AM" : "PM");
    }
    lv_label_set_text(c_time, buf);
    strftime(buf, sizeof(buf), "%a %d %b %Y", &ti);
    lv_label_set_text(c_date, buf);
    set_hand(c_hand_s, pts_s, ti.tm_sec * 6.0f, 88);
    set_hand(c_hand_m, pts_m, ti.tm_min * 6.0f + ti.tm_sec * 0.1f, 76);
    set_hand(c_hand_h, pts_h, (ti.tm_hour % 12) * 30.0f + ti.tm_min * 0.5f, 52);
}

// ---- Navigation ----
static int active_tab()
{
    lv_obj_t *act = lv_tileview_get_tile_act(tv);
    int w = lv_obj_get_width(tv);
    return (act != nullptr && w > 0) ? lv_obj_get_x(act) / w : 0;
}

static void update_tabs()
{
    int idx = active_tab();
    for (int k = 0; k < TAB_COUNT; k++) {
        lv_obj_set_style_text_color(tab_lbl[k],
            k == idx ? lv_palette_main(LV_PALETTE_LIGHT_BLUE) : lv_palette_main(LV_PALETTE_GREY), 0);
        lv_label_set_text(tab_lbl[k], k == idx ? TAB_TEXT[k] : TAB_ICON[k]);
    }
}

static void on_tv_change(lv_event_t *e)
{
    update_tabs();
}

static void on_tab_click(lv_event_t *e)
{
    lv_obj_set_tile_id(tv, (uint32_t)(intptr_t)lv_event_get_user_data(e), 0, LV_ANIM_ON);
}

static void on_row_click(lv_event_t *e)
{
    selectedSvc = (int)(intptr_t)lv_event_get_user_data(e);
    render_detail_nolock();
    lv_obj_clear_flag(dpage, LV_OBJ_FLAG_HIDDEN);
}

static void on_back_click(lv_event_t *e)
{
    lv_obj_add_flag(dpage, LV_OBJ_FLAG_HIDDEN);
}

static void on_banner_tap(lv_event_t *e)
{
    int first = -1;
    for (int i = 0; i < MAX_SERVICES; i++) {
        if (confirmedDown[i]) {
            if (first < 0 && !acked[i]) first = i;
            acked[i] = true;
        }
    }
    alarmActive = false;
    lv_obj_add_flag(banner, LV_OBJ_FLAG_HIDDEN);
    if (first >= 0 && first < uiCount) {
        selectedSvc = first;
        render_detail_nolock();
        lv_obj_clear_flag(dpage, LV_OBJ_FLAG_HIDDEN);
    }
    Serial.println("Alarm acknowledged");
}

// ---- Incident history page ----
static void render_hist_nolock()
{
    static char buf[1000];
    incidents_format(buf, sizeof(buf));
    lv_label_set_text(hist_label, buf);
    hist_shown = incidents_version();
}

static void on_hist_open(lv_event_t *e)
{
    render_hist_nolock();
    lv_obj_scroll_to_y(hist_box, 0, LV_ANIM_OFF);
    lv_obj_clear_flag(hpage, LV_OBJ_FLAG_HIDDEN);
}

static void on_hist_back(lv_event_t *e)
{
    lv_obj_add_flag(hpage, LV_OBJ_FLAG_HIDDEN);
}

// ---- Home page ----
static void on_act_scan(lv_event_t *e)  { nettools_open("Scan networks"); }
static void on_act_ping(lv_event_t *e)  { nettools_open("Ping"); }
static void on_act_speed(lv_event_t *e) { nettools_open("Speed test"); }
static void on_act_check(lv_event_t *e)
{
    checkNow = true;
    lv_label_set_text(h_caption, "Checking now...");
    lv_obj_set_style_text_color(h_caption, lv_palette_main(LV_PALETTE_BLUE), 0);
}

static void update_home_nolock(int up, int total)
{
    if (!h_arc) return;
    int conf = 0, n = 0;
    long sum = 0;
    unsigned long checks = 0, upc = 0;
    bool anyHas = false;
    for (int i = 0; i < uiCount; i++) {
        if (confirmedDown[i]) conf++;
        const History &h = hist[i];
        if (h.has) anyHas = true;
        if (h.has && h.lastOk) { sum += h.lastMs; n++; }
        checks += h.checks;
        upc += h.upChecks;
    }
    lv_color_t col;
    if (total == 0) {
        col = lv_palette_main(LV_PALETTE_GREY);
        lv_label_set_text(h_caption, "No services set up");
    } else if (!anyHas) {
        col = lv_palette_main(LV_PALETTE_BLUE);
        lv_label_set_text(h_caption, "Checking services...");
    } else if (conf > 0) {
        col = lv_palette_main(LV_PALETTE_RED);
        if (conf == 1) lv_label_set_text(h_caption, "1 service is down");
        else lv_label_set_text_fmt(h_caption, "%d services are down", conf);
    } else if (up < total) {
        col = lv_palette_main(LV_PALETTE_AMBER);
        lv_label_set_text(h_caption, "Checking a failure...");
    } else if (n && sum / n > 3000) {
        col = lv_palette_main(LV_PALETTE_AMBER);
        lv_label_set_text(h_caption, "All up, but slow");
    } else {
        col = lv_palette_main(LV_PALETTE_GREEN);
        lv_label_set_text(h_caption, "All systems operational");
    }
    lv_obj_set_style_arc_color(h_arc, col, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(h_caption, col, 0);
    lv_arc_set_value(h_arc, total ? (up * 100 / total) : 0);
    lv_label_set_text_fmt(h_count, "%d/%d", up, total);
    if (n) {
        int v = (int)(sum / n);
        if (v < 1000) lv_label_set_text_fmt(h_avg, "%d ms", v);
        else lv_label_set_text_fmt(h_avg, "%d.%d s", v / 1000, (v % 1000) / 100);
    } else {
        lv_label_set_text(h_avg, "--");
    }
    if (checks) lv_label_set_text_fmt(h_up, "%d%%", (int)(upc * 100UL / checks));
    else lv_label_set_text(h_up, "--");
}

// Once a second: header clock, signal, and the return-to-Home timer.
static void on_home_timer(lv_timer_t *t)
{
    struct tm ti;
    if (getLocalTime(&ti, 0)) { char tb[12]; fmt_hm(tb, sizeof(tb), ti); lv_label_set_text(h_time, tb); }
    else lv_label_set_text(h_time, "--:--");

    bool conn = WiFi.status() == WL_CONNECTED;
    int r = conn ? WiFi.RSSI() : -127;
    lv_color_t c = !conn ? lv_palette_main(LV_PALETTE_RED)
                 : r >= -60 ? lv_palette_main(LV_PALETTE_GREEN)
                 : r >= -72 ? lv_palette_main(LV_PALETTE_AMBER)
                 : lv_palette_main(LV_PALETTE_RED);
    lv_obj_set_style_text_color(h_wifi, c, 0);
    lv_obj_set_style_text_color(h_sig, c, 0);
    if (conn) lv_label_set_text_fmt(h_sig, "%d dBm", r);
    else lv_label_set_text(h_sig, "offline");

    if (hpage && !lv_obj_has_flag(hpage, LV_OBJ_FLAG_HIDDEN) && hist_shown != incidents_version()) render_hist_nolock();

    // after 90 s without a touch, go back to Home (unless a tool is busy, e.g. the signal meter)
    if (ui_set.autoReturnS > 0 && lv_disp_get_inactive_time(NULL) > (uint32_t)ui_set.autoReturnS * 1000UL &&
        !(nettools_is_open() && nettools_is_busy())) {
        if (!lv_obj_has_flag(dpage, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(dpage, LV_OBJ_FLAG_HIDDEN);
        if (!lv_obj_has_flag(hpage, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(hpage, LV_OBJ_FLAG_HIDDEN);
        if (spage && !lv_obj_has_flag(spage, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(spage, LV_OBJ_FLAG_HIDDEN);
        if (nettools_is_open()) nettools_close();
        if (active_tab() != 0) lv_obj_set_tile_id(tv, 0, 0, LV_ANIM_ON);
    }
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, int w, int x, int y,
                             lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, w, 34);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}

static void style_action(lv_obj_t *b)
{
    lv_obj_set_style_bg_color(b, lv_palette_darken(LV_PALETTE_BLUE, 2), 0);
    lv_obj_set_style_bg_color(b, lv_palette_main(LV_PALETTE_BLUE), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(lv_obj_get_child(b, 0), lv_color_white(), 0);
}

static void make_stat(lv_obj_t *parent, int x, const char *title, lv_obj_t **value)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, 70, 46);
    lv_obj_align(c, LV_ALIGN_TOP_LEFT, x, 180);
    lv_obj_set_style_radius(c, 8, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_set_style_bg_color(c, C_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    paint(c);
    lv_obj_t *t = lv_label_create(c);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 4);
    *value = lv_label_create(c);
    lv_obj_set_style_text_font(*value, &lv_font_montserrat_16, 0);
    lv_label_set_text(*value, "--");
    lv_obj_align(*value, LV_ALIGN_BOTTOM_MID, 0, -5);
}

// ---- Settings page ----
static void restart_cb(lv_timer_t *t)
{
    ESP.restart();
}

static void on_settings_open(lv_event_t *e)
{
    lv_obj_clear_flag(spage, LV_OBJ_FLAG_HIDDEN);
}

static void on_settings_back(lv_event_t *e)
{
    lv_obj_add_flag(spage, LV_OBJ_FLAG_HIDDEN);
}

static void on_set_theme(lv_event_t *e)
{
    ui_set.darkTheme = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    ui_settings_save();
    lv_timer_create(restart_cb, 700, nullptr);   // colours are applied when the UI is built
}

static void on_set_alerts(lv_event_t *e)
{
    ui_set.alertsOn = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    ui_settings_save();
    if (!ui_set.alertsOn) {
        alarmActive = false;
        lv_obj_add_flag(banner, LV_OBJ_FLAG_HIDDEN);
    } else {
        checkNow = true;   // re-evaluate straight away so a standing outage shows again
    }
}

static void on_set_mute(lv_event_t *e)
{
    ui_set.buzzerMute = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    ui_settings_save();
    if (ui_set.buzzerMute) alarmActive = false;
}

static const uint16_t RETURN_OPTS[3] = {30, 90, 0};

static void set_return_text()
{
    if (ui_set.autoReturnS == 0) lv_label_set_text(s_return_lbl, "Off");
    else lv_label_set_text_fmt(s_return_lbl, "%u s", (unsigned)ui_set.autoReturnS);
    lv_obj_center(s_return_lbl);
}

static void on_set_return(lv_event_t *e)
{
    int idx = 0;
    for (int k = 0; k < 3; k++) {
        if (RETURN_OPTS[k] == ui_set.autoReturnS) idx = k;
    }
    ui_set.autoReturnS = RETURN_OPTS[(idx + 1) % 3];
    ui_settings_save();
    set_return_text();
}

static void set_clock_text()
{
    lv_label_set_text(s_clock_lbl, ui_set.clock24h ? "24 h" : "12 h");
    lv_obj_center(s_clock_lbl);
}

static void on_set_clock(lv_event_t *e)
{
    ui_set.clock24h = !ui_set.clock24h;
    ui_settings_save();
    set_clock_text();
}

static void settings_row(lv_obj_t *parent, const char *text, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 10, y + 8);
}

static void settings_switch(lv_obj_t *parent, int y, bool on, lv_event_cb_t cb)
{
    lv_obj_t *s = lv_switch_create(parent);
    lv_obj_align(s, LV_ALIGN_TOP_RIGHT, -10, y + 4);
    if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s, cb, LV_EVENT_VALUE_CHANGED, nullptr);
}

static void settings_build_page()
{
    spage = lv_obj_create(lv_scr_act());
    lv_obj_set_size(spage, 240, 320);
    lv_obj_set_pos(spage, 0, 0);
    lv_obj_set_style_radius(spage, 0, 0);
    lv_obj_set_style_border_width(spage, 0, 0);
    lv_obj_set_style_pad_all(spage, 0, 0);
    lv_obj_set_style_bg_color(spage, C_BG, 0);
    lv_obj_set_style_bg_opa(spage, LV_OPA_COVER, 0);
    paint(spage);
    lv_obj_clear_flag(spage, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(spage, LV_OBJ_FLAG_HIDDEN);

    make_button(spage, "< Back", 84, 8, 6, on_settings_back, nullptr);
    lv_obj_t *title = lv_label_create(spage);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_RIGHT, -10, 14);

    int y = 52;
    settings_row(spage, "Dark theme", y);
    settings_switch(spage, y, ui_set.darkTheme, on_set_theme);
    y += 46;
    settings_row(spage, "Alerts", y);
    settings_switch(spage, y, ui_set.alertsOn, on_set_alerts);
    y += 46;
    settings_row(spage, "Buzzer mute", y);
    settings_switch(spage, y, ui_set.buzzerMute, on_set_mute);
    y += 46;
    settings_row(spage, "Auto-return", y);
    lv_obj_t *rb = make_button(spage, "", 84, 146, y, on_set_return, nullptr);
    s_return_lbl = lv_obj_get_child(rb, 0);
    set_return_text();
    y += 46;
    settings_row(spage, "Clock", y);
    lv_obj_t *cb = make_button(spage, "", 84, 146, y, on_set_clock, nullptr);
    s_clock_lbl = lv_obj_get_child(cb, 0);
    set_clock_text();

    lv_obj_t *hint = lv_label_create(spage);
    lv_label_set_text(hint, "Changing the theme restarts the board");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 10, 288);
}

static void build_ui()
{
    lv_obj_set_style_bg_color(lv_scr_act(), C_BG, 0);

    tv = lv_tileview_create(lv_scr_act());
    lv_obj_set_size(tv, LV_PCT(100), 280);
    lv_obj_align(tv, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(tv, C_BG, 0);
    paint(tv);
    lv_dir_t both = (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_t *t0 = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
    lv_obj_t *t1 = lv_tileview_add_tile(tv, 1, 0, both);
    lv_obj_t *t2 = lv_tileview_add_tile(tv, 2, 0, both);
    lv_obj_t *t3 = lv_tileview_add_tile(tv, 3, 0, both);
    lv_obj_t *t4 = lv_tileview_add_tile(tv, 4, 0, LV_DIR_LEFT);
    paint(t0); paint(t1); paint(t2); paint(t3); paint(t4);

    // ---- Page 0: Home dashboard ----
    lv_obj_t *h_name = lv_label_create(t0);
    lv_label_set_text(h_name, DEVICE_NAME);
    lv_obj_set_style_text_font(h_name, &lv_font_montserrat_16, 0);
    lv_obj_align(h_name, LV_ALIGN_TOP_LEFT, 10, 8);

    h_time = lv_label_create(t0);
    lv_obj_set_style_text_font(h_time, &lv_font_montserrat_16, 0);
    lv_label_set_text(h_time, "--:--");
    lv_obj_align(h_time, LV_ALIGN_TOP_RIGHT, -10, 8);

    h_wifi = lv_label_create(t0);
    lv_obj_set_style_text_font(h_wifi, &lv_font_montserrat_16, 0);
    lv_label_set_text(h_wifi, LV_SYMBOL_WIFI);
    lv_obj_align(h_wifi, LV_ALIGN_TOP_RIGHT, -68, 8);

    lv_obj_t *h_gear = lv_btn_create(t0);      // opens the Settings page
    lv_obj_set_size(h_gear, 34, 28);
    lv_obj_align(h_gear, LV_ALIGN_TOP_RIGHT, -92, 3);
    lv_obj_set_style_bg_opa(h_gear, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(h_gear, 0, 0);
    lv_obj_set_style_pad_all(h_gear, 0, 0);
    lv_obj_add_event_cb(h_gear, on_settings_open, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *h_gear_lbl = lv_label_create(h_gear);
    lv_label_set_text(h_gear_lbl, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(h_gear_lbl, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_center(h_gear_lbl);

    h_arc = lv_arc_create(t0);
    lv_obj_set_size(h_arc, 120, 120);
    lv_obj_align(h_arc, LV_ALIGN_TOP_MID, 0, 30);
    lv_arc_set_rotation(h_arc, 270);
    lv_arc_set_bg_angles(h_arc, 0, 360);
    lv_arc_set_range(h_arc, 0, 100);
    lv_arc_set_value(h_arc, 0);
    lv_obj_remove_style(h_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(h_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(h_arc, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_width(h_arc, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(h_arc, C_CARD, LV_PART_MAIN);
    lv_obj_set_style_arc_color(h_arc, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);

    h_count = lv_label_create(t0);
    lv_obj_set_style_text_font(h_count, &lv_font_montserrat_30, 0);
    lv_label_set_text(h_count, "--");
    lv_obj_align_to(h_count, h_arc, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *h_tap = lv_obj_create(t0);       // invisible: tap the ring to open the incident log
    lv_obj_set_size(h_tap, 120, 120);
    lv_obj_align(h_tap, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_bg_opa(h_tap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(h_tap, 0, 0);
    lv_obj_set_style_radius(h_tap, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(h_tap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(h_tap, on_hist_open, LV_EVENT_CLICKED, nullptr);

    h_caption = lv_label_create(t0);
    lv_obj_set_style_text_font(h_caption, &lv_font_montserrat_16, 0);
    lv_label_set_text(h_caption, "Starting...");
    lv_obj_set_style_text_align(h_caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(h_caption, 232);
    lv_obj_align(h_caption, LV_ALIGN_TOP_MID, 0, 154);

    make_stat(t0, 8, "Avg", &h_avg);
    make_stat(t0, 85, "Signal", &h_sig);
    lv_obj_set_style_text_font(h_sig, &lv_font_montserrat_14, 0);
    make_stat(t0, 162, "Uptime", &h_up);

    style_action(make_button(t0, "Scan", 52, 8, 236, on_act_scan, nullptr));
    style_action(make_button(t0, "Ping", 52, 66, 236, on_act_ping, nullptr));
    style_action(make_button(t0, "Speed", 52, 124, 236, on_act_speed, nullptr));
    style_action(make_button(t0, "Check", 52, 182, 236, on_act_check, nullptr));

    // ---- Page 1: status list ----
    lv_obj_t *title = lv_label_create(t1);
    lv_label_set_text(title, "Services");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_30, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    ip_label = lv_label_create(t1);          // still updated by the monitor task; shown on the Info page
    lv_label_set_text(ip_label, "");
    lv_obj_add_flag(ip_label, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < MAX_SERVICES; i++) {
        rows[i] = lv_obj_create(t1);
        lv_obj_set_size(rows[i], 224, 40);
        lv_obj_align(rows[i], LV_ALIGN_TOP_LEFT, 8, 46 + i * 41);
        lv_obj_set_style_pad_all(rows[i], 0, 0);
        lv_obj_set_style_radius(rows[i], 8, 0);
        lv_obj_set_style_border_width(rows[i], 0, 0);
        lv_obj_set_style_bg_color(rows[i], C_CARD, 0);
        lv_obj_set_style_bg_opa(rows[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(rows[i], lv_palette_darken(LV_PALETTE_BLUE, 3), LV_STATE_PRESSED);
        paint(rows[i]);
        lv_obj_clear_flag(rows[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(rows[i], on_row_click, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        dots[i] = lv_obj_create(rows[i]);
        lv_obj_set_size(dots[i], 16, 16);
        lv_obj_set_style_radius(dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dots[i], 0, 0);
        lv_obj_set_style_bg_color(dots[i], lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_clear_flag(dots[i], (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_align(dots[i], LV_ALIGN_LEFT_MID, 8, 0);

        names[i] = lv_label_create(rows[i]);
        lv_label_set_text(names[i], "");
        lv_obj_set_style_text_font(names[i], &lv_font_montserrat_16, 0);
        lv_label_set_long_mode(names[i], LV_LABEL_LONG_DOT);
        lv_obj_set_size(names[i], 180, 20);
        lv_obj_align(names[i], LV_ALIGN_TOP_LEFT, 32, 0);

        infos[i] = lv_label_create(rows[i]);
        lv_label_set_text(infos[i], "");
        lv_obj_set_style_text_font(infos[i], &lv_font_montserrat_16, 0);
        lv_obj_align(infos[i], LV_ALIGN_TOP_LEFT, 32, 19);

        lv_obj_add_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
    }

    summary = lv_label_create(t1);
    lv_label_set_text(summary, "");
    lv_obj_set_style_text_font(summary, &lv_font_montserrat_16, 0);
    lv_obj_align(summary, LV_ALIGN_TOP_MID, 0, 252);

    // ---- Page 2: tools menu (see nettools.cpp) ----
    nettools_build_menu(t2);

    // ---- Page 3: clock ----
    lv_obj_t *face = lv_obj_create(t3);
    lv_obj_set_size(face, 200, 200);
    lv_obj_align(face, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(face, 0, 0);
    lv_obj_set_style_border_width(face, 0, 0);
    lv_obj_set_style_outline_width(face, 3, 0);
    lv_obj_set_style_outline_color(face, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_bg_color(face, C_CARD, 0);
    lv_obj_set_style_bg_opa(face, LV_OPA_COVER, 0);
    lv_obj_clear_flag(face, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

    for (int k = 0; k < 12; k++) {
        int sz = (k % 3 == 0) ? 10 : 6;
        float a = k * 30 * 0.0174533f;
        int x = (int)(100 + 88 * sinf(a));
        int y = (int)(100 - 88 * cosf(a));
        lv_obj_t *tick = lv_obj_create(face);
        lv_obj_set_size(tick, sz, sz);
        lv_obj_set_style_radius(tick, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(tick, 0, 0);
        lv_obj_set_style_bg_color(tick, C_TEXT, 0);
        lv_obj_clear_flag(tick, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_set_pos(tick, x - sz / 2, y - sz / 2);
    }

    lv_obj_t *hands[3];
    lv_color_t hand_col[3] = {C_TEXT, C_TEXT, lv_palette_main(LV_PALETTE_RED)};
    int hand_w[3] = {6, 4, 2};
    for (int k = 0; k < 3; k++) {
        hands[k] = lv_line_create(face);
        lv_obj_set_size(hands[k], 200, 200);
        lv_obj_set_pos(hands[k], 0, 0);
        lv_obj_set_style_line_width(hands[k], hand_w[k], 0);
        lv_obj_set_style_line_color(hands[k], hand_col[k], 0);
        lv_obj_set_style_line_rounded(hands[k], true, 0);
        lv_obj_clear_flag(hands[k], LV_OBJ_FLAG_CLICKABLE);
    }
    c_hand_h = hands[0];
    c_hand_m = hands[1];
    c_hand_s = hands[2];

    lv_obj_t *cap = lv_obj_create(face);
    lv_obj_set_size(cap, 10, 10);
    lv_obj_set_style_radius(cap, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(cap, 0, 0);
    lv_obj_set_style_bg_color(cap, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_clear_flag(cap, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_pos(cap, 95, 95);

    c_time = lv_label_create(t3);
    lv_obj_set_style_text_font(c_time, &lv_font_montserrat_30, 0);
    lv_label_set_text(c_time, "--:--:--");
    lv_obj_align(c_time, LV_ALIGN_TOP_MID, 0, 214);

    c_date = lv_label_create(t3);
    lv_obj_set_style_text_font(c_date, &lv_font_montserrat_16, 0);
    lv_label_set_text(c_date, "Syncing time...");
    lv_obj_align(c_date, LV_ALIGN_TOP_MID, 0, 250);
    lv_timer_create(on_clock_timer, 1000, nullptr);

    // ---- Page 4: device info ----
    lv_obj_t *info_title = lv_label_create(t4);
    lv_label_set_text(info_title, "Device Info");
    lv_obj_set_style_text_font(info_title, &lv_font_montserrat_30, 0);
    lv_obj_align(info_title, LV_ALIGN_TOP_MID, 0, 6);

    info_label = lv_label_create(t4);
    lv_obj_set_style_text_font(info_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_line_space(info_label, 4, 0);
    lv_obj_set_width(info_label, 224);
    lv_obj_align(info_label, LV_ALIGN_TOP_LEFT, 8, 46);
    lv_label_set_text(info_label, "");
    lv_timer_create(on_info_timer, 1000, nullptr);

    // ---- Tab bar ----
    lv_obj_t *bar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(bar, 240, 40);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, C_CARD, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    for (int k = 0; k < TAB_COUNT; k++) {
        lv_obj_t *b = lv_btn_create(bar);
        lv_obj_set_size(b, 48, 40);
        lv_obj_set_pos(b, k * 48, 0);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_set_style_pad_all(b, 0, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_30, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_add_event_cb(b, on_tab_click, LV_EVENT_CLICKED, (void *)(intptr_t)k);
        tab_lbl[k] = lv_label_create(b);
        lv_label_set_text(tab_lbl[k], TAB_TEXT[k]);
        lv_obj_set_style_text_align(tab_lbl[k], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(tab_lbl[k]);
    }
    lv_obj_add_event_cb(tv, on_tv_change, LV_EVENT_VALUE_CHANGED, nullptr);
    update_tabs();

    // ---- Service detail page: full screen, on top of the tabs ----
    dpage = lv_obj_create(lv_scr_act());
    lv_obj_set_size(dpage, 240, 320);
    lv_obj_set_pos(dpage, 0, 0);
    lv_obj_set_style_radius(dpage, 0, 0);
    lv_obj_set_style_border_width(dpage, 0, 0);
    lv_obj_set_style_pad_all(dpage, 0, 0);
    lv_obj_set_style_bg_color(dpage, C_BG, 0);
    lv_obj_set_style_bg_opa(dpage, LV_OPA_COVER, 0);
    paint(dpage);
    lv_obj_clear_flag(dpage, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(dpage, LV_OBJ_FLAG_HIDDEN);

    make_button(dpage, "< Back", 84, 8, 6, on_back_click, nullptr);

    d_name = lv_label_create(dpage);
    lv_obj_set_style_text_font(d_name, &lv_font_montserrat_16, 0);
    lv_label_set_long_mode(d_name, LV_LABEL_LONG_DOT);
    lv_obj_set_size(d_name, 224, 20);
    lv_obj_align(d_name, LV_ALIGN_TOP_LEFT, 8, 42);

    d_status = lv_label_create(dpage);
    lv_obj_set_style_text_font(d_status, &lv_font_montserrat_30, 0);
    lv_obj_align(d_status, LV_ALIGN_TOP_LEFT, 8, 62);

    d_url = lv_label_create(dpage);
    lv_label_set_long_mode(d_url, LV_LABEL_LONG_DOT);
    lv_obj_set_size(d_url, 224, 34);
    lv_obj_align(d_url, LV_ALIGN_TOP_LEFT, 8, 104);

    d_stats = lv_label_create(dpage);
    lv_obj_set_width(d_stats, 224);
    lv_obj_set_style_text_line_space(d_stats, 4, 0);
    lv_obj_align(d_stats, LV_ALIGN_TOP_LEFT, 8, 142);

    d_chart = lv_chart_create(dpage);
    lv_obj_set_size(d_chart, 224, 62);
    lv_obj_align(d_chart, LV_ALIGN_TOP_LEFT, 8, 204);
    lv_obj_set_style_bg_color(d_chart, C_CHART_BG, 0);
    lv_obj_set_style_border_width(d_chart, 0, 0);
    lv_obj_set_style_line_color(d_chart, C_GRID, LV_PART_MAIN);
    lv_chart_set_type(d_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(d_chart, HIST_LEN);
    lv_chart_set_range(d_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 500);
    lv_chart_set_div_line_count(d_chart, 3, 0);
    lv_obj_set_style_size(d_chart, 0, LV_PART_INDICATOR);
    d_series = lv_chart_add_series(d_chart, lv_palette_main(LV_PALETTE_LIGHT_BLUE), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(d_chart, d_series, LV_CHART_POINT_NONE);
    render_detail_nolock();

    // ---- Incident history page: full screen, on top of the tabs ----
    hpage = lv_obj_create(lv_scr_act());
    lv_obj_set_size(hpage, 240, 320);
    lv_obj_set_pos(hpage, 0, 0);
    lv_obj_set_style_radius(hpage, 0, 0);
    lv_obj_set_style_border_width(hpage, 0, 0);
    lv_obj_set_style_pad_all(hpage, 0, 0);
    lv_obj_set_style_bg_color(hpage, C_BG, 0);
    lv_obj_set_style_bg_opa(hpage, LV_OPA_COVER, 0);
    paint(hpage);
    lv_obj_clear_flag(hpage, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(hpage, LV_OBJ_FLAG_HIDDEN);

    make_button(hpage, "< Back", 84, 8, 6, on_hist_back, nullptr);
    lv_obj_t *hist_title = lv_label_create(hpage);
    lv_label_set_text(hist_title, "Incidents");
    lv_obj_set_style_text_font(hist_title, &lv_font_montserrat_16, 0);
    lv_obj_align(hist_title, LV_ALIGN_TOP_RIGHT, -10, 14);

    hist_box = lv_obj_create(hpage);
    lv_obj_set_size(hist_box, 224, 264);
    lv_obj_align(hist_box, LV_ALIGN_TOP_LEFT, 8, 48);
    lv_obj_set_style_bg_opa(hist_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hist_box, 0, 0);
    lv_obj_set_style_pad_all(hist_box, 0, 0);
    paint(hist_box);
    hist_label = lv_label_create(hist_box);
    lv_obj_set_width(hist_label, 216);
    lv_label_set_long_mode(hist_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(hist_label, 3, 0);
    lv_label_set_text(hist_label, "");

    // ---- Settings page: full screen, on top of the tabs ----
    settings_build_page();

    // ---- Tool page (nettools.cpp): full screen, on top of the tabs ----
    nettools_build_page(lv_scr_act());

    // ---- Alarm banner: top of the screen, above everything ----
    banner = lv_obj_create(lv_scr_act());
    lv_obj_set_size(banner, 232, 46);
    lv_obj_align(banner, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_bg_color(banner, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_border_width(banner, 0, 0);
    lv_obj_clear_flag(banner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(banner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(banner, on_banner_tap, LV_EVENT_CLICKED, nullptr);
    banner_label = lv_label_create(banner);
    lv_obj_set_style_text_color(banner_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(banner_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(banner_label, "");
    lv_obj_center(banner_label);

    lv_timer_create(on_home_timer, 1000, nullptr);
}

// Shows `count` rows with fresh names and hides the rest. Also clears the history.
static void refresh_rows(const Service *snap, int count)
{
    lvgl_port_lock(-1);
    memcpy(uiSvc, snap, sizeof(uiSvc));
    uiCount = count;
    memset(hist, 0, sizeof(hist));
    for (int i = 0; i < MAX_SERVICES; i++) {
        if (i < count) {
            lv_label_set_text(names[i], snap[i].name);
            lv_label_set_text(infos[i], "waiting...");
            lv_obj_set_style_bg_color(dots[i], lv_palette_main(LV_PALETTE_GREY), 0);
            lv_obj_clear_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (selectedSvc >= count) {
        selectedSvc = -1;
        lv_obj_add_flag(dpage, LV_OBJ_FLAG_HIDDEN);
    }
    render_detail_nolock();
    update_info_nolock();
    update_home_nolock(0, count);
    lvgl_port_unlock();
}

static void update_row(int i, bool ok, uint32_t ms, int code)
{
    lvgl_port_lock(-1);
    lv_obj_set_style_bg_color(dots[i],
        lv_palette_main(ok ? LV_PALETTE_GREEN : LV_PALETTE_RED), 0);
    if (ok) lv_label_set_text_fmt(infos[i], "UP    %lu ms", (unsigned long)ms);
    else    lv_label_set_text_fmt(infos[i], "DOWN  (%d)", code);

    History &h = hist[i];
    h.ms[h.head] = ok ? (int16_t)(ms > 32000 ? 32000 : ms) : (int16_t)-1;
    h.head = (h.head + 1) % HIST_LEN;
    if (h.n < HIST_LEN) h.n++;
    h.checks++;
    if (ok) h.upChecks++;
    h.lastMs = ms;
    h.lastCode = code;
    h.lastOk = ok;
    h.has = true;
    if (i == selectedSvc) render_detail_nolock();
    lvgl_port_unlock();
}

static void update_summary(int up, int total)
{
    lvgl_port_lock(-1);
    lv_label_set_text_fmt(summary, "%d / %d services up", up, total);
    update_home_nolock(up, total);
    lvgl_port_unlock();
}

static void update_alarm(const Service *snap, int count)
{
    int unacked = 0;
    int first = -1;
    for (int i = 0; i < count; i++) {
        if (confirmedDown[i] && !acked[i]) {
            unacked++;
            if (first < 0) first = i;
        }
    }
    if (!ui_set.alertsOn) unacked = 0;
    alarmActive = unacked > 0 && !ui_set.buzzerMute;

    lvgl_port_lock(-1);
    if (unacked == 0) {
        lv_obj_add_flag(banner, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (unacked == 1) {
            lv_label_set_text_fmt(banner_label, "%s DOWN\nTap to silence", snap[first].name);
        } else {
            lv_label_set_text_fmt(banner_label, "%s +%d DOWN\nTap to silence",
                                  snap[first].name, unacked - 1);
        }
        lv_obj_center(banner_label);
        lv_obj_clear_flag(banner, LV_OBJ_FLAG_HIDDEN);
    }
    lvgl_port_unlock();
}

// ================= Monitoring =================
// ---- Service checks ----
// Each check returns an HTTP-style code: 200-399 = up, a negative number = down.
// -1 connection failed, -100 bad address, -300 keyword not found.
static const uint32_t TLS_MIN_BLOCK = 20000;   // bytes of one free heap block needed for a TLS handshake
static const int CHECK_SKIPPED = -200;         // check could not run (low memory, tool busy): not a failure

int nettools_ping(const char *host, uint32_t timeout_ms, uint32_t &rtt);   // nettools.cpp

// Reads the start of the response body and looks for the word. Case sensitive, first 16 KB only.
static bool body_contains(HTTPClient &http, const char *word, uint32_t timeout_ms)
{
    size_t n = strlen(word);
    if (n == 0) return true;
    if (n > 23) n = 23;
    WiFiClient *s = http.getStreamPtr();
    uint8_t win[24 + 256];
    size_t keep = 0, total = 0;
    uint32_t t0 = millis();
    while (total < 16384 && millis() - t0 < timeout_ms) {
        int avail = s->available();
        if (avail <= 0) {
            if (!http.connected()) break;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        int r = s->read(win + keep, avail > 256 ? 256 : avail);
        if (r <= 0) continue;
        total += r;
        size_t have = keep + r;
        for (size_t i = 0; i + n <= have; i++) {
            if (memcmp(win + i, word, n) == 0) return true;
        }
        keep = have < n - 1 ? have : n - 1;
        memmove(win, win + have - keep, keep);
    }
    return false;
}

static int web_get(HTTPClient &http, const Service &s, uint32_t t0, uint32_t &ms)
{
    int code = http.GET();
    ms = millis() - t0;
    if (s.word[0] && code > 0 && code < 400 && !body_contains(http, s.word, 6000)) code = -300;
    http.end();
    return code;
}

static int check_web(const Service &s, uint32_t &ms)
{
    HTTPClient http;
    http.setTimeout(6000);
    http.setConnectTimeout(6000);
    if (s.word[0]) http.useHTTP10(true);   // plain body, no chunked encoding to confuse the word search
    uint32_t t0 = millis();
    if (strncmp(s.url, "https", 5) == 0) {
        WiFiClientSecure client;
        client.setInsecure();   // reachability check only: certificates are not verified
        client.setHandshakeTimeout(8);   // seconds; the default is 120
        if (!http.begin(client, s.url)) return -100;
        return web_get(http, s, t0, ms);
    }
    WiFiClient client;
    if (!http.begin(client, s.url)) return -100;
    return web_get(http, s, t0, ms);
}

static int check_tcp(const char *spec, uint32_t &ms)   // spec = "host:port"
{
    String hp(spec);
    hp.trim();
    int colon = hp.lastIndexOf(':');
    int port = colon < 1 ? 0 : hp.substring(colon + 1).toInt();
    if (colon < 1 || port < 1 || port > 65535) {
        Serial.printf("tcp: bad address '%s' (needs tcp://host:port)\n", hp.c_str());
        return -100;
    }
    String host = hp.substring(0, colon);
    uint32_t t0 = millis();
    IPAddress ip;
    if (!ip.fromString(host) && WiFi.hostByName(host.c_str(), ip) != 1) {
        ms = millis() - t0;
        return -1;
    }
    WiFiClient c;
    bool ok = c.connect(ip, (uint16_t)port, 4000);
    ms = millis() - t0;
    c.stop();
    return ok ? 200 : -1;
}

// True when Wi-Fi is up. Right after a (re)connect it waits 5 s so DNS and routes can settle.
static bool wifi_settled()
{
    static bool was = false;
    static uint32_t upAt = 0;
    bool c = WiFi.status() == WL_CONNECTED;
    if (c && !was) upAt = millis();
    was = c;
    while (c && millis() - upAt < 5000) {
        vTaskDelay(pdMS_TO_TICKS(250));
        c = WiFi.status() == WL_CONNECTED;
        if (!c) was = false;
    }
    return c;
}

static int check_service(const Service &s, uint32_t &ms)
{
    if (!wifi_settled()) return CHECK_SKIPPED;   // the board itself is offline: say nothing about the service
    if (strncmp(s.url, "ping://", 7) == 0) {
        uint32_t rtt = 0;
        int r = nettools_ping(s.url + 7, 1000, rtt);   // 1 reply, 0 no reply, -1 ping helper busy
        if (r < 0) return CHECK_SKIPPED;
        ms = rtt;
        return r ? 200 : -1;
    }

    // Web checks. A TLS handshake needs one big free heap block (PSRAM is not enabled in this build).
    // Without it the connect fails at once with "SSL - Memory allocation failed", which says nothing
    // about the service: wait and retry, and if memory never frees up report CHECK_SKIPPED.
    bool tls = strncmp(s.url, "https", 5) == 0;
    bool lowHeap = false;
    int code = -1;
    for (int attempt = 0; attempt < 3; attempt++) {
        if (tls && ESP.getMaxAllocHeap() < TLS_MIN_BLOCK) {
            lowHeap = true;
            Serial.printf("check: largest free block %u bytes, free heap %u, waiting\n",
                          (unsigned)ESP.getMaxAllocHeap(), (unsigned)ESP.getFreeHeap());
            vTaskDelay(pdMS_TO_TICKS(1500));
            continue;
        }
        lowHeap = false;
        uint32_t t0 = millis();
        code = check_web(s, ms);
        bool failedFast = (code == -1) && (millis() - t0 < 2000);   // never really reached the network
        if (!failedFast) return code;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return lowHeap ? CHECK_SKIPPED : code;
}

// ---- Phone alerts through ntfy.sh ----
// Messages are queued by service_event() and sent by the monitor task at the end of each round,
// so a TLS session never overlaps a service check (the heap only fits one at a time).
enum { NOTE_TEST = 0, NOTE_DOWN = 1, NOTE_UP = 2 };

// ---- Settings kept in flash, quiet hours, and the event log shown on the web page ----
static volatile int brightNormal = 100, brightDim = 15, dimAfterS = 60;   // percent, percent, seconds (0 = never dim)
static volatile int quietFrom = 0, quietTo = 0;                          // hours 0-23; equal = quiet hours off

static void settings_load()
{
    prefs.begin("statusboard", true);
    brightNormal = prefs.getInt("bn", 100);
    brightDim = prefs.getInt("bd", 15);
    dimAfterS = prefs.getInt("da", 60);
    quietFrom = prefs.getInt("qf", 0);
    quietTo = prefs.getInt("qt", 0);
    prefs.end();
}

static void settings_save()
{
    prefs.begin("statusboard", false);
    prefs.putInt("bn", brightNormal);
    prefs.putInt("bd", brightDim);
    prefs.putInt("da", dimAfterS);
    prefs.putInt("qf", quietFrom);
    prefs.putInt("qt", quietTo);
    prefs.end();
}

static bool quiet_now()
{
    if (quietFrom == quietTo) return false;
    time_t now = time(nullptr);
    if (now < 1700000000) return false;   // clock not set yet
    struct tm t;
    localtime_r(&now, &t);
    int h = t.tm_hour;
    return quietFrom < quietTo ? (h >= quietFrom && h < quietTo) : (h >= quietFrom || h < quietTo);
}

#define LOG_N 60
#define LOG_W 80
static char logbuf[LOG_N][LOG_W];
static volatile int logHead = 0, logCount = 0;

// Written by the monitor task only; the web page copies lines out of it.
static void weblog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void weblog(const char *fmt, ...)
{
    char *line = logbuf[logHead];
    int off;
    time_t now = time(nullptr);
    if (now > 1700000000) {
        struct tm t;
        localtime_r(&now, &t);
        off = snprintf(line, LOG_W, "%02d:%02d:%02d ", t.tm_hour, t.tm_min, t.tm_sec);
    } else {
        off = snprintf(line, LOG_W, "+%lus ", (unsigned long)(millis() / 1000));
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line + off, LOG_W - off, fmt, ap);
    va_end(ap);
    logHead = (logHead + 1) % LOG_N;
    if (logCount < LOG_N) logCount++;
}

static bool notify_configured()
{
    bool ok;
    xSemaphoreTake(cfgMutex, portMAX_DELAY);
    ok = ntTopic[0] != 0;
    xSemaphoreGive(cfgMutex);
    return ok;
}

// Returns 1 = sent (or nothing to do), 0 = failed, -1 = not enough memory right now, try later.
static int notify_send(const char *text, uint8_t kind)
{
    char topic[sizeof(ntTopic)];
    xSemaphoreTake(cfgMutex, portMAX_DELAY);
    strlcpy(topic, ntTopic, sizeof(topic));
    xSemaphoreGive(cfgMutex);
    if (!topic[0]) return 1;
    if (WiFi.status() != WL_CONNECTED) return -1;
    if (ESP.getMaxAllocHeap() < TLS_MIN_BLOCK) return -1;

    WiFiClientSecure client;
    client.setInsecure();   // certificates are not verified (as for the service checks)
    client.setHandshakeTimeout(8);   // seconds; the default is 120
    HTTPClient http;
    http.setTimeout(8000);
    http.setConnectTimeout(8000);
    String url = String("https://ntfy.sh/") + topic;
    if (!http.begin(client, url)) return 0;
    http.addHeader("Title", DEVICE_NAME);
    http.addHeader("Priority", kind == NOTE_DOWN ? "high" : "default");
    http.addHeader("Tags", kind == NOTE_DOWN ? "rotating_light" : kind == NOTE_UP ? "white_check_mark" : "bell");
    int code = http.POST(String(text));
    http.end();
    if (code == 200) { Serial.println("notify: sent"); weblog("phone alert sent"); }
    else { Serial.printf("notify: HTTP %d\n", code); weblog("phone alert failed: HTTP %d", code); }
    return code == 200 ? 1 : 0;
}

struct NoteMsg { char text[96]; uint8_t kind; uint8_t tries; };
static const int NOTE_QUEUE = 6;
static NoteMsg noteQ[NOTE_QUEUE];
static int noteCount = 0;      // monitor task only

static void notify_queue(const char *text, uint8_t kind)
{
    weblog("%s", text);
    if (kind == NOTE_TEST && quiet_now() && strcmp(text, "Phone alerts are on") != 0) {
        weblog("  (held back: quiet hours)");   // informational messages only; DOWN and UP always go out
        return;
    }
    if (!notify_configured()) return;
    if (noteCount >= NOTE_QUEUE) {                   // full: drop the oldest
        memmove(&noteQ[0], &noteQ[1], sizeof(NoteMsg) * (NOTE_QUEUE - 1));
        noteCount = NOTE_QUEUE - 1;
    }
    strlcpy(noteQ[noteCount].text, text, sizeof(noteQ[noteCount].text));
    noteQ[noteCount].kind = kind;
    noteQ[noteCount].tries = 0;
    noteCount++;
}

static void notify_flush()
{
    while (noteCount > 0) {
        int r = notify_send(noteQ[0].text, noteQ[0].kind);
        bool drop = (r == 1) || (r == 0 && ++noteQ[0].tries >= 4);   // give up after 4 failed rounds
        if (!drop) break;                                             // try again next round
        memmove(&noteQ[0], &noteQ[1], sizeof(NoteMsg) * (noteCount - 1));
        noteCount--;
    }
}

// One place for service up/down events: the incident log and the phone alert.
static const int RECOVER_OKS = 2;              // OK checks in a row before a DOWN service counts as recovered
static int oks[MAX_SERVICES];
static uint32_t downSince[MAX_SERVICES];

static void service_event(const char *name, bool down, uint32_t downSecs = 0)
{
    if (down) incidents_start(name);
    else incidents_end(name);
    char msg[96];
    if (down) snprintf(msg, sizeof(msg), "%s is DOWN", name);
    else if (downSecs >= 120) snprintf(msg, sizeof(msg), "%s is back UP after %lu min", name, (unsigned long)(downSecs / 60));
    else if (downSecs > 0) snprintf(msg, sizeof(msg), "%s is back UP after %lu s", name, (unsigned long)downSecs);
    else snprintf(msg, sizeof(msg), "%s is back UP", name);
    notify_queue(msg, down ? NOTE_DOWN : NOTE_UP);
}

static uint32_t lastRun[MAX_SERVICES];   // when each service was last checked (0 = not yet)
static uint8_t slowRuns[MAX_SERVICES];   // consecutive checks slower than the service's slow= limit

// Runs in its own FreeRTOS task so network waits never block the UI.
static void monitor_task(void *)
{
    while (WiFi.status() != WL_CONNECTED) vTaskDelay(pdMS_TO_TICKS(500));

    lvgl_port_lock(-1);
    lv_label_set_text_fmt(ip_label, "Config: http://%s", WiFi.localIP().toString().c_str());
    lvgl_port_unlock();

    configTzTime(CLOCK_TZ, "pool.ntp.org", "time.google.com");

    {   // report unexpected restarts; restarts caused by saving on the web page stay quiet
        esp_reset_reason_t rr = esp_reset_reason();
        const char *why = nullptr;
        if (rr == ESP_RST_PANIC) why = "a crash";
        else if (rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT) why = "a watchdog reset";
        else if (rr == ESP_RST_BROWNOUT) why = "a power dip (brownout)";
        if (why) {
            char m[64];
            snprintf(m, sizeof(m), "Board restarted after %s", why);
            notify_queue(m, NOTE_TEST);
        }
    }

    Service snap[MAX_SERVICES];
    int count = 0;

    for (;;) {
        bool changed = cfgChanged;
        cfgChanged = false;

        xSemaphoreTake(cfgMutex, portMAX_DELAY);
        memcpy(snap, cfg, sizeof(snap));
        count = cfgCount;
        xSemaphoreGive(cfgMutex);

        if (changed) {
            const char *nm[MAX_SERVICES];
            for (int i = 0; i < count; i++) nm[i] = snap[i].name;
            incidents_close_missing(nm, count);
            for (int i = 0; i < MAX_SERVICES; i++) {
                fails[i] = 0;
                confirmedDown[i] = false;
                acked[i] = false;
                lastRun[i] = 0;
                slowRuns[i] = 0;
            }
            refresh_rows(snap, count);
            update_alarm(snap, count);
        }

        int up = 0;
        for (int i = 0; i < count; i++) {
            if (cfgChanged) break;   // config edited mid-round: restart with the new list
            if (snap[i].every && lastRun[i] &&
                millis() - lastRun[i] + 2000 < (uint32_t)snap[i].every * 1000UL) {
                if (!confirmedDown[i]) up++;   // not due yet: keep the last known state
                continue;
            }
            uint32_t ms = 0;
            int code = check_service(snap[i], ms);
            lastRun[i] = millis();
            if (code == CHECK_SKIPPED) {
                Serial.printf("%-12s skipped (no Wi-Fi, low memory or tool busy), not counted\n", snap[i].name);
                if (!confirmedDown[i]) up++;
                continue;
            }
            bool ok = code > 0 && code < 400;
            if (ok) up++;
            Serial.printf("%-12s code=%d  %lu ms  %s\n",
                          snap[i].name, code, (unsigned long)ms, ok ? "UP" : "DOWN");
            if (!ok) weblog("%s check failed (code %d, %lu ms)", snap[i].name, code, (unsigned long)ms);
            if (ok && snap[i].slow && !confirmedDown[i]) {
                char sm[96];
                if ((int)ms > snap[i].slow) {
                    if (slowRuns[i] < 255 && ++slowRuns[i] == 3) {
                        snprintf(sm, sizeof(sm), "%s is slow: %lu ms (limit %u)",
                                 snap[i].name, (unsigned long)ms, (unsigned)snap[i].slow);
                        notify_queue(sm, NOTE_TEST);
                    }
                } else {
                    if (slowRuns[i] >= 3) {
                        snprintf(sm, sizeof(sm), "%s speed is back to normal", snap[i].name);
                        notify_queue(sm, NOTE_TEST);
                    }
                    slowRuns[i] = 0;
                }
            }

            if (ok) {
                if (confirmedDown[i] && ++oks[i] < RECOVER_OKS) {
                    // answered, but not trusted yet: stays DOWN until RECOVER_OKS OK checks in a row
                } else {
                    if (confirmedDown[i]) {
                        Serial.printf("RECOVERED: %s\n", snap[i].name);
                        service_event(snap[i].name, false, (millis() - downSince[i]) / 1000);
                    }
                    fails[i] = 0;
                    oks[i] = 0;
                    confirmedDown[i] = false;
                    acked[i] = false;
                }
            } else {
                oks[i] = 0;
                if (++fails[i] >= FAIL_THRESHOLD && !confirmedDown[i]) {
                    confirmedDown[i] = true;
                    acked[i] = false;
                    downSince[i] = millis();
                    Serial.printf("ALERT: %s is DOWN\n", snap[i].name);
                    service_event(snap[i].name, true);
                }
            }
            update_row(i, ok, ms, code);
        }
        if (!cfgChanged) {
            update_summary(up, count);
            update_alarm(snap, count);
        }

        if (ntTest) {
            ntTest = false;
            notify_queue("Phone alerts are on", NOTE_TEST);
        }
        {   // report a Wi-Fi outage of a minute or more once the link is back (time is seen in check rounds)
            static uint32_t lostAt = 0;
            if (WiFi.status() != WL_CONNECTED) {
                if (!lostAt) lostAt = millis();
            } else if (lostAt) {
                uint32_t secs = (millis() - lostAt) / 1000;
                lostAt = 0;
                if (secs >= 60) {
                    char m[64];
                    snprintf(m, sizeof(m), "Wi-Fi was down for at least %lu min", (unsigned long)(secs / 60));
                    notify_queue(m, NOTE_TEST);
                }
            }
        }
        notify_flush();

        // Wait for the next round, but wake immediately if the config changes.
        for (uint32_t w = 0; w < CHECK_INTERVAL_MS && !cfgChanged && !checkNow; w += 500) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        checkNow = false;
    }
}

static void buzzer_task(void *)
{
#if BUZZER_PIN >= 0
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
#else
    Serial.println("Buzzer disabled (BUZZER_PIN not set)");
#endif
    for (;;) {
        if (alarmActive) {
#if BUZZER_PIN >= 0
  #if BUZZER_PASSIVE
            tone(BUZZER_PIN, 2200, 150);
  #else
            digitalWrite(BUZZER_PIN, HIGH);
            vTaskDelay(pdMS_TO_TICKS(150));
            digitalWrite(BUZZER_PIN, LOW);
  #endif
#endif
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

// ================= Arduino entry points =================
// ---- Display brightness: dims after a period without touch, wakes on touch ----
static Board *gBoard = nullptr;
// brightness limits come from brightNormal / brightDim / dimAfterS (set on /tools)

static void backlight_task(void *)
{
    int cur = -1;
    for (;;) {
        bool alarm = false;
        for (int i = 0; i < MAX_SERVICES; i++) {
            if (confirmedDown[i] && !acked[i]) alarm = true;   // keep the screen bright while something is DOWN
        }
        lvgl_port_lock(-1);
        uint32_t idle = lv_disp_get_inactive_time(NULL);
        lvgl_port_unlock();
        int want = (alarm || dimAfterS == 0 || idle < (uint32_t)dimAfterS * 1000UL) ? brightNormal : brightDim;
        if (want != cur && gBoard && gBoard->getBacklight()) {
            gBoard->getBacklight()->setBrightness(want);
            cur = want;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ---- /tools: settings, export, event log, Wake-on-LAN ----
static int arg_int(const char *name, int lo, int hi, int def)
{
    if (!server.hasArg(name)) return def;
    int v = server.arg(name).toInt();
    return v < lo ? lo : (v > hi ? hi : v);
}

static void handle_tools()
{
    if (!authed()) return;
    String mac;
    prefs.begin("statusboard", true);
    mac = prefs.getString("wolmac", "");
    prefs.end();
    String page = F("<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
                    "<title>" DEVICE_NAME " tools</title></head>"
                    "<body style='font-family:sans-serif;max-width:520px;margin:20px auto;padding:0 12px'>"
                    "<h2>More tools</h2><h3>Settings</h3><form method=POST action=/settings>");
    page += F("<p>Brightness when active, 5 to 100 %: <input name=bn type=number min=5 max=100 value=");
    page += String((int)brightNormal);
    page += F("><br>Brightness when dimmed, 1 to 100 %: <input name=bd type=number min=1 max=100 value=");
    page += String((int)brightDim);
    page += F("><br>Dim after this many seconds without a touch (0 = never): <input name=da type=number min=0 max=3600 value=");
    page += String((int)dimAfterS);
    page += F("></p>");
    page += F("<p>Quiet hours: informational alerts (slow service, restart, Wi-Fi outage) are held back from hour ");
    page += String((int)quietFrom);
    page += F(" to hour <input name=qt type=number min=0 max=23 value=");
    page += String((int)quietTo);
    page += F("> (0 to 23, board clock). Same number twice = off. DOWN and back-UP alerts are never held back.<br>"
              "From hour: <input name=qf type=number min=0 max=23 value=");
    page += String((int)quietFrom);
    page += F("></p><button type=submit style='padding:8px 16px'>Save settings</button></form>"
              "<h3>Services</h3><p><a href=/export>Download the services list</a> "
              "(to import, paste it into the Services box on the main page and save).</p>"
              "<h3>Events</h3><p><a href=/log>Recent alerts and failed checks</a></p>"
              "<h3>Wake-on-LAN</h3><form method=POST action=/wol>MAC address: "
              "<input name=mac placeholder='AA:BB:CC:DD:EE:FF' value='");
    page += html_escape(mac);
    page += F("'> <button type=submit style='padding:8px 16px'>Send</button></form>"
              "<p><a href=/>Back</a></p></body></html>");
    server.send(200, "text/html", page);
}

static void handle_settings_save()
{
    if (!authed()) return;
    brightNormal = arg_int("bn", 5, 100, brightNormal);
    brightDim = arg_int("bd", 1, 100, brightDim);
    dimAfterS = arg_int("da", 0, 3600, dimAfterS);
    quietFrom = arg_int("qf", 0, 23, quietFrom);
    quietTo = arg_int("qt", 0, 23, quietTo);
    settings_save();
    server.sendHeader("Location", "/tools");
    server.send(303);
}

static void handle_export()
{
    if (!authed()) return;
    String out;
    xSemaphoreTake(cfgMutex, portMAX_DELAY);
    for (int i = 0; i < cfgCount; i++) {
        out += cfg[i].name;
        out += ", ";
        out += cfg[i].url;
        if (cfg[i].word[0]) { out += ", contains="; out += cfg[i].word; }
        if (cfg[i].every) { out += ", every="; out += String((int)cfg[i].every); }
        if (cfg[i].slow) { out += ", slow="; out += String((int)cfg[i].slow); }
        out += '\n';
    }
    xSemaphoreGive(cfgMutex);
    server.sendHeader("Content-Disposition", "attachment; filename=pocketnoc-services.txt");
    server.send(200, "text/plain", out);
}

static void handle_log()
{
    if (!authed()) return;
    String page = F("<!doctype html><html><head><meta http-equiv=refresh content=10>"
                    "<meta name=viewport content='width=device-width,initial-scale=1'>"
                    "<title>" DEVICE_NAME " events</title></head>"
                    "<body style='font-family:sans-serif;max-width:640px;margin:20px auto;padding:0 12px'>"
                    "<h2>Recent events</h2><p>Newest first. Refreshes every 10 seconds. "
                    "Kept in memory only, so a restart clears it.</p>"
                    "<pre style='white-space:pre-wrap;font-size:13px'>");
    int cnt = logCount, head = logHead;
    for (int k = 1; k <= cnt; k++) {
        char line[LOG_W];
        strlcpy(line, logbuf[(head - k + LOG_N) % LOG_N], sizeof(line));
        page += html_escape(String(line));
        page += '\n';
    }
    if (cnt == 0) page += "(nothing yet)";
    page += F("</pre><p><a href=/tools>Back</a></p></body></html>");
    server.send(200, "text/html", page);
}

static void handle_wol()
{
    if (!authed()) return;
    String mac = server.arg("mac");
    mac.trim();
    uint8_t b[6] = {0};
    int n = 0, nib = -1;
    bool bad = false;
    for (unsigned k = 0; k < mac.length() && !bad; k++) {
        char c = mac[k];
        int v = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (v >= 0) {
            if (nib < 0) nib = v;
            else { if (n < 6) b[n] = (uint8_t)((nib << 4) | v); n++; nib = -1; }
        } else if (c == ':' || c == '-') {
            if (nib >= 0) bad = true;   // single hex digit before a separator
        } else {
            bad = true;
        }
    }
    if (bad || nib >= 0 || n != 6) {
        server.send(400, "text/html", "<!doctype html><body style='font-family:sans-serif;padding:20px'>"
                    "That is not a MAC address. Use six pairs like AA:BB:CC:DD:EE:FF. <a href=/tools>Back</a></body>");
        return;
    }
    uint8_t pkt[102];
    memset(pkt, 0xFF, 6);
    for (int k = 0; k < 16; k++) memcpy(pkt + 6 + k * 6, b, 6);
    WiFiUDP udp;
    bool ok = udp.beginPacket(WiFi.broadcastIP(), 9) && udp.write(pkt, sizeof(pkt)) == sizeof(pkt) && udp.endPacket();
    prefs.begin("statusboard", false);
    prefs.putString("wolmac", mac);
    prefs.end();
    server.send(200, "text/html", String("<!doctype html><body style='font-family:sans-serif;padding:20px'>") +
                (ok ? "Magic packet sent to " : "Could not send the packet to ") + html_escape(mac) +
                ". <a href=/tools>Back</a></body>");
}

void setup()
{
    Serial.begin(115200);
    delay(1500);

    cfgMutex = xSemaphoreCreateMutex();
    incidents_begin();
    load_config();
    ui_settings_load();
    settings_load();

    Serial.println("Initializing board");
    Board *board = new Board();
    board->init();
#if LVGL_PORT_AVOID_TEARING_MODE
    auto lcd = board->getLCD();
    lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
    auto lcd_bus = lcd->getBus();
    if (lcd_bus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        static_cast<BusRGB *>(lcd_bus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * 10);
    }
#endif
#endif
    assert(board->begin());

    gBoard = board;   // used by backlight_task

    Serial.println("Initializing LVGL");
    lvgl_port_init(board->getLCD(), board->getTouch());

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);   // WiFiMulti (see wifi_service) handles joining and rejoining
    wifi_add_networks();

    lvgl_port_lock(-1);
    build_ui();
    lvgl_port_unlock();

    xTaskCreatePinnedToCore(monitor_task, "monitor", 12288, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(buzzer_task, "buzzer", 3072, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(backlight_task, "backlight", 3072, nullptr, 1, nullptr, 0);
    Serial.println("UI ready");
}

void loop()
{
    wifi_service();
    if (otaStarted) ArduinoOTA.handle();
    if (WiFi.status() == WL_CONNECTED || apActive) {
        if (!serverStarted) {
            static const char *hdrs[] = {"Authorization", "Cookie"};
            server.collectHeaders(hdrs, 2);
            server.on("/", HTTP_GET, handle_root);
            server.on("/login", HTTP_GET, handle_login_page);
            server.on("/login", HTTP_POST, handle_login);
            server.on("/logout", HTTP_GET, handle_logout);
            server.on("/favicon.ico", HTTP_GET, []() { server.send(204); });
            server.on("/save", HTTP_POST, handle_save);
            server.on("/wifi", HTTP_GET, handle_wifi_page);
            server.on("/wifi", HTTP_POST, handle_wifi_save);
            server.on("/tools", HTTP_GET, handle_tools);
            server.on("/settings", HTTP_POST, handle_settings_save);
            server.on("/export", HTTP_GET, handle_export);
            server.on("/log", HTTP_GET, handle_log);
            server.on("/wol", HTTP_POST, handle_wol);
            server.begin();
            serverStarted = true;
            ArduinoOTA.setHostname(DEVICE_HOST);
            ArduinoOTA.setPassword(OTA_PASSWORD);
            ArduinoOTA.setMdnsEnabled(false);   // main.cpp already runs mDNS
            ArduinoOTA.onStart([]() { Serial.println("OTA: update starting"); });
            ArduinoOTA.begin();
            otaStarted = true;
            if (MDNS.begin(DEVICE_HOST)) {
                MDNS.addService("http", "tcp", 80);
                Serial.println("mDNS: http://" DEVICE_HOST ".local/");
            }
            Serial.printf("Config page: http://%s/\n", (apActive ? WiFi.softAPIP() : WiFi.localIP()).toString().c_str());
        }
        server.handleClient();
    }
    static uint32_t lastNote = 0;
    if (WiFi.status() != WL_CONNECTED && millis() - lastNote > 5000) {
        lastNote = millis();
        Serial.printf("Wi-Fi not connected (status %d)%s\n", (int)WiFi.status(), apActive ? ", setup mode" : "");
        lvgl_port_lock(-1);
        lv_label_set_text(summary, apActive ? "Setup mode: see Info page" : "Wi-Fi: looking for a network");
        lvgl_port_unlock();
    }
    delay(5);
}
