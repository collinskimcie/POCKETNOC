#!/usr/bin/env python3
"""PocketNOC round 3b: handshake timeout, recovery rule, Wi-Fi settle, board event alerts.
Run from the project root. Every edit must match exactly once, or NOTHING is written.
Undo with: git checkout src/main.cpp
"""
import sys
from pathlib import Path

MAIN = Path("src/main.cpp")
if not MAIN.exists():
    sys.exit("src/main.cpp not found. Run this from the project root.")
raw = MAIN.read_bytes().decode("utf-8")
crlf = "\r\n" in raw
main = raw.replace("\r\n", "\n")

if "RECOVER_OKS" in main:
    sys.exit("Round 3b looks applied already (RECOVER_OKS is in src/main.cpp).")

failures = []


def edit(text, label, old, new):
    n = text.count(old)
    if n != 1:
        failures.append(f"{label}: expected 1 match, found {n}")
        return text
    return text.replace(old, new)


# 1. TLS handshake timeout (service checks and ntfy)
main = edit(main, "handshake: checks",
    "        client.setInsecure();   // reachability check only: certificates are not verified\n",
    "        client.setInsecure();   // reachability check only: certificates are not verified\n"
    "        client.setHandshakeTimeout(8);   // seconds; the default is 120\n")
main = edit(main, "handshake: ntfy",
    "    client.setInsecure();   // certificates are not verified (as for the service checks)\n",
    "    client.setInsecure();   // certificates are not verified (as for the service checks)\n"
    "    client.setHandshakeTimeout(8);   // seconds; the default is 120\n")

# 2. clearer tcp:// address errors
main = edit(main, "check_tcp parsing",
    r'''    String hp(spec);
    int colon = hp.lastIndexOf(':');
    if (colon < 1) return -100;
    String host = hp.substring(0, colon);
    int port = hp.substring(colon + 1).toInt();
    if (port < 1 || port > 65535) return -100;
''',
    r'''    String hp(spec);
    hp.trim();
    int colon = hp.lastIndexOf(':');
    int port = colon < 1 ? 0 : hp.substring(colon + 1).toInt();
    if (colon < 1 || port < 1 || port > 65535) {
        Serial.printf("tcp: bad address '%s' (needs tcp://host:port)\n", hp.c_str());
        return -100;
    }
    String host = hp.substring(0, colon);
''')

# 3. Wi-Fi settle delay after (re)connect
main = edit(main, "wifi settle",
    '''static int check_service(const Service &s, uint32_t &ms)
{
    if (WiFi.status() != WL_CONNECTED) return CHECK_SKIPPED;   // the board itself is offline: say nothing about the service
''',
    r'''// True when Wi-Fi is up. Right after a (re)connect it waits 5 s so DNS and routes can settle.
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
''')

main = edit(main, "skipped message",
    r'''Serial.printf("%-12s skipped (low memory), not counted\n", snap[i].name);''',
    r'''Serial.printf("%-12s skipped (no Wi-Fi, low memory or tool busy), not counted\n", snap[i].name);''')

# 4. recovery alert with duration
main = edit(main, "service_event",
    r'''static void service_event(const char *name, bool down)
{
    if (down) incidents_start(name);
    else incidents_end(name);
    char msg[96];
    snprintf(msg, sizeof(msg), "%s %s", name, down ? "is DOWN" : "is back UP");
    notify_queue(msg, down ? NOTE_DOWN : NOTE_UP);
}
''',
    r'''static const int RECOVER_OKS = 2;              // OK checks in a row before a DOWN service counts as recovered
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
''')

main = edit(main, "monitor up/down logic",
    r'''            if (ok) {
                if (confirmedDown[i]) {
                    Serial.printf("RECOVERED: %s\n", snap[i].name);
                    service_event(snap[i].name, false);
                }
                fails[i] = 0;
                confirmedDown[i] = false;
                acked[i] = false;
            } else if (++fails[i] >= FAIL_THRESHOLD && !confirmedDown[i]) {
                confirmedDown[i] = true;
                acked[i] = false;
                Serial.printf("ALERT: %s is DOWN\n", snap[i].name);
                service_event(snap[i].name, true);
            }
''',
    r'''            if (ok) {
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
''')

# 5. board events: unexpected restart
main = edit(main, "restart alert",
    '''    configTzTime(CLOCK_TZ, "pool.ntp.org", "time.google.com");

    Service snap[MAX_SERVICES];
''',
    r'''    configTzTime(CLOCK_TZ, "pool.ntp.org", "time.google.com");

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
''')

# 6. board events: Wi-Fi outage report
main = edit(main, "wifi outage alert",
    '''        notify_flush();

        // Wait for the next round, but wake immediately if the config changes.
''',
    r'''        {   // report a Wi-Fi outage of a minute or more once the link is back (time is seen in check rounds)
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
''')

if failures:
    print("NOTHING was written. These edits did not match:")
    for f in failures:
        print("  -", f)
    print("Paste this output and I will adjust the script.")
    sys.exit(1)

MAIN.write_bytes((main.replace("\n", "\r\n") if crlf else main).encode("utf-8"))
print("Done: src/main.cpp updated.")
print("Next: pio run -t upload   (undo: git checkout src/main.cpp)")
