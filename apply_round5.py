#!/usr/bin/env python3
"""PocketNOC round 5: /tools settings page, quiet hours, export, live log, Wake-on-LAN.
Run from the project root. Every edit must match exactly once, or NOTHING is written.
Undo with: git checkout src/main.cpp
"""
import re
import sys
from pathlib import Path

MAIN = Path("src/main.cpp")
if not MAIN.exists():
    sys.exit("src/main.cpp not found. Run this from the project root.")
raw = MAIN.read_bytes().decode("utf-8")
crlf = "\r\n" in raw
main = raw.replace("\r\n", "\n")
if "weblog(" in main:
    sys.exit("Round 5 looks applied already (weblog is in src/main.cpp).")

has_opts = "uint16_t every" in main          # round 4 options present
has_dim = "backlight_task" in main           # round 4 auto-dim present
failures = []
notes = []


def edit(text, label, old, new):
    n = text.count(old)
    if n != 1:
        failures.append(f"{label}: expected 1 match, found {n}")
        return text
    return text.replace(old, new)


# ------------------------------------------------------------ includes
main = edit(main, "udp include",
    "using namespace esp_panel::drivers;\n",
    "#include <WiFiUdp.h>\nusing namespace esp_panel::drivers;\n")

# ------------------------------------------------------------ settings, quiet hours, event log
BLOCK_A = r'''// ---- Settings kept in flash, quiet hours, and the event log shown on the web page ----
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

'''
main = edit(main, "settings block",
    "static bool notify_configured()\n{\n    bool ok;\n",
    BLOCK_A + "static bool notify_configured()\n{\n    bool ok;\n")

main = edit(main, "log notify result",
    r'''    if (code == 200) Serial.println("notify: sent");
    else Serial.printf("notify: HTTP %d\n", code);
''',
    r'''    if (code == 200) { Serial.println("notify: sent"); weblog("phone alert sent"); }
    else { Serial.printf("notify: HTTP %d\n", code); weblog("phone alert failed: HTTP %d", code); }
''')

main = edit(main, "notify_queue log and quiet hours",
    "static void notify_queue(const char *text, uint8_t kind)\n{\n    if (!notify_configured()) return;\n",
    r'''static void notify_queue(const char *text, uint8_t kind)
{
    weblog("%s", text);
    if (kind == NOTE_TEST && quiet_now() && strcmp(text, "Phone alerts are on") != 0) {
        weblog("  (held back: quiet hours)");   // informational messages only; DOWN and UP always go out
        return;
    }
    if (!notify_configured()) return;
''')

main = edit(main, "log failed checks",
    'snap[i].name, code, (unsigned long)ms, ok ? "UP" : "DOWN");\n',
    'snap[i].name, code, (unsigned long)ms, ok ? "UP" : "DOWN");\n'
    '            if (!ok) weblog("%s check failed (code %d, %lu ms)", snap[i].name, code, (unsigned long)ms);\n')

main = edit(main, "load settings at boot",
    "    load_config();\n    ui_settings_load();\n",
    "    load_config();\n    ui_settings_load();\n    settings_load();\n")

# ------------------------------------------------------------ brightness uses the saved settings
if has_dim:
    pat = re.compile(r'^static const int BRIGHT_NORMAL.*\n^static const int BRIGHT_DIM.*\n^static const uint32_t DIM_AFTER_MS.*\n', re.M)
    found = pat.findall(main)
    if len(found) != 1:
        failures.append(f"brightness constants: expected 1 block, found {len(found)}")
    else:
        main = pat.sub("// brightness limits come from brightNormal / brightDim / dimAfterS (set on /tools)\n", main)
    main = edit(main, "dim logic",
        "int want = (alarm || idle < DIM_AFTER_MS) ? BRIGHT_NORMAL : BRIGHT_DIM;",
        "int want = (alarm || dimAfterS == 0 || idle < (uint32_t)dimAfterS * 1000UL) ? brightNormal : brightDim;")
else:
    notes.append("Auto-dim (round 4) is not in main.cpp, so the /tools page has no brightness fields.")

# ------------------------------------------------------------ web handlers
DIM_FORM = r'''    page += F("<p>Brightness when active, 5 to 100 %: <input name=bn type=number min=5 max=100 value=");
    page += String((int)brightNormal);
    page += F("><br>Brightness when dimmed, 1 to 100 %: <input name=bd type=number min=1 max=100 value=");
    page += String((int)brightDim);
    page += F("><br>Dim after this many seconds without a touch (0 = never): <input name=da type=number min=0 max=3600 value=");
    page += String((int)dimAfterS);
    page += F("></p>");
'''
OPTS_EXPORT = r'''        if (cfg[i].every) { out += ", every="; out += String((int)cfg[i].every); }
        if (cfg[i].slow) { out += ", slow="; out += String((int)cfg[i].slow); }
'''

BLOCK_B = r'''// ---- /tools: settings, export, event log, Wake-on-LAN ----
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
@DIMFORM@    page += F("<p>Quiet hours: informational alerts (slow service, restart, Wi-Fi outage) are held back from hour ");
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
@OPTSEXPORT@        out += '\n';
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

'''
BLOCK_B = BLOCK_B.replace("@DIMFORM@", DIM_FORM if has_dim else "").replace("@OPTSEXPORT@", OPTS_EXPORT if has_opts else "")

main = edit(main, "handlers",
    "void setup()\n{\n    Serial.begin(115200);\n",
    BLOCK_B + "void setup()\n{\n    Serial.begin(115200);\n")

main = edit(main, "routes",
    '            server.on("/wifi", HTTP_POST, handle_wifi_save);\n',
    '            server.on("/wifi", HTTP_POST, handle_wifi_save);\n'
    '            server.on("/tools", HTTP_GET, handle_tools);\n'
    '            server.on("/settings", HTTP_POST, handle_settings_save);\n'
    '            server.on("/export", HTTP_GET, handle_export);\n'
    '            server.on("/log", HTTP_GET, handle_log);\n'
    '            server.on("/wol", HTTP_POST, handle_wol);\n')

# optional: link from the Wi-Fi page (not fatal if it does not match)
link_old = '"</form><p><a href=/>Back</a></p></body></html>");'
if main.count(link_old) == 1:
    main = main.replace(link_old, '"</form><p><a href=/>Back</a> | <a href=/tools>More tools</a></p></body></html>");')
else:
    notes.append("Could not add the 'More tools' link to the Wi-Fi page; open http://pocketnoc.local/tools directly.")

if failures:
    print("NOTHING was written. These edits did not match:")
    for f in failures:
        print("  -", f)
    print("Paste this output and I will adjust the script.")
    sys.exit(1)

MAIN.write_bytes((main.replace("\n", "\r\n") if crlf else main).encode("utf-8"))
print("Done: src/main.cpp updated.")
for n in notes:
    print("Note:", n)
print("Next: pio run -t upload   (undo: git checkout src/main.cpp)")
