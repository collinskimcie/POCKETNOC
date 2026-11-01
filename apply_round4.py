#!/usr/bin/env python3
"""PocketNOC round 4: per-service every=/slow=, auto-dim, OTA updates.
Run from the project root. Every edit must match exactly once, or NOTHING is written.
Skip a feature with --skip=options,dim,ota   Undo: git checkout src/main.cpp src/esp_panel_board_custom_conf.h platformio.ini .gitignore
"""
import re
import secrets
import sys
from pathlib import Path

SKIP = set()
for a in sys.argv[1:]:
    if a.startswith("--skip="):
        SKIP |= set(a[7:].split(","))

MAIN = Path("src/main.cpp")
CONF = Path("src/esp_panel_board_custom_conf.h")
if not MAIN.exists():
    sys.exit("src/main.cpp not found. Run this from the project root.")
raw = MAIN.read_bytes().decode("utf-8")
crlf = "\r\n" in raw
main = raw.replace("\r\n", "\n")
if "lastRun" in main:
    sys.exit("Round 4 looks applied already (lastRun is in src/main.cpp).")

failures = []


def edit(text, label, old, new):
    n = text.count(old)
    if n != 1:
        failures.append(f"{label}: expected 1 match, found {n}")
        return text
    return text.replace(old, new)


# ---------------------------------------------------------------- options: every= and slow=
if "options" not in SKIP:
    main = edit(main, "Service struct",
        "struct Service { char name[24]; char url[110]; char word[24]; };",
        "struct Service { char name[24]; char url[110]; char word[24]; uint16_t every; uint16_t slow; };")

    main = edit(main, "parse options",
        r'''        String word = "";
        int c2 = url.lastIndexOf(',');
        if (c2 >= 0) {
            String tail = url.substring(c2 + 1);
            tail.trim();
            if (tail.startsWith("contains=")) {
                word = tail.substring(9);
                word.trim();
                url = url.substring(0, c2);
            }
        }
        name.trim();
''',
        r'''        String word = "";
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
''')

    main = edit(main, "parse assign",
        r'''        strlcpy(out[n].word, word.c_str(), sizeof(out[n].word));
        n++;
''',
        r'''        strlcpy(out[n].word, word.c_str(), sizeof(out[n].word));
        out[n].every = (uint16_t)every;
        out[n].slow = (uint16_t)slow;
        n++;
''')

    main = edit(main, "help text",
        'in the first 16 KB).</p>"',
        'in the first 16 KB). A line can also end with <code>, every=60</code> (check at most every 60 seconds, '
        'minimum 10) and <code>, slow=1500</code> (alert when 3 checks in a row take longer than 1500 ms).</p>"')

    main = edit(main, "monitor declarations",
        "// Runs in its own FreeRTOS task so network waits never block the UI.\n",
        r'''static uint32_t lastRun[MAX_SERVICES];   // when each service was last checked (0 = not yet)
static uint8_t slowRuns[MAX_SERVICES];   // consecutive checks slower than the service's slow= limit

// Runs in its own FreeRTOS task so network waits never block the UI.
''')

    main = edit(main, "monitor config reset",
        "                fails[i] = 0;\n                confirmedDown[i] = false;\n                acked[i] = false;\n            }\n            refresh_rows(snap, count);",
        "                fails[i] = 0;\n                confirmedDown[i] = false;\n                acked[i] = false;\n                lastRun[i] = 0;\n                slowRuns[i] = 0;\n            }\n            refresh_rows(snap, count);")

    main = edit(main, "monitor due check",
        "            uint32_t ms = 0;\n            int code = check_service(snap[i], ms);\n",
        r'''            if (snap[i].every && lastRun[i] &&
                millis() - lastRun[i] + 2000 < (uint32_t)snap[i].every * 1000UL) {
                if (!confirmedDown[i]) up++;   // not due yet: keep the last known state
                continue;
            }
            uint32_t ms = 0;
            int code = check_service(snap[i], ms);
            lastRun[i] = millis();
''')

    main = edit(main, "monitor slow alert",
        '''            Serial.printf("%-12s code=%d  %lu ms  %s\\n",
                          snap[i].name, code, (unsigned long)ms, ok ? "UP" : "DOWN");
''',
        r'''            Serial.printf("%-12s code=%d  %lu ms  %s\n",
                          snap[i].name, code, (unsigned long)ms, ok ? "UP" : "DOWN");
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
''')

# ---------------------------------------------------------------- dim: auto-dim backlight
conf_new = None
if "dim" not in SKIP:
    main = edit(main, "dim task",
        "void setup()\n{\n    Serial.begin(115200);\n",
        r'''// ---- Display brightness: dims after a period without touch, wakes on touch ----
static Board *gBoard = nullptr;
static const int BRIGHT_NORMAL = 100;            // percent
static const int BRIGHT_DIM = 15;                // percent
static const uint32_t DIM_AFTER_MS = 60000;      // idle time before dimming

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
        int want = (alarm || idle < DIM_AFTER_MS) ? BRIGHT_NORMAL : BRIGHT_DIM;
        if (want != cur && gBoard && gBoard->getBacklight()) {
            gBoard->getBacklight()->setBrightness(want);
            cur = want;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void setup()
{
    Serial.begin(115200);
''')

    test_re = re.compile(
        r'    // backlight test: dim to 30% at boot \(remove after the experiment\)\n    \{\n.*?\n    \}\n', re.S)
    found = list(test_re.finditer(main))
    if len(found) == 1:
        m0 = found[0]
        main = main[:m0.start()] + "    gBoard = board;   // used by backlight_task\n" + main[m0.end():]
    elif len(found) == 0:
        main = edit(main, "gBoard assign",
            "    assert(board->begin());\n",
            "    assert(board->begin());\n    gBoard = board;   // used by backlight_task\n")
    else:
        failures.append("backlight test block: found more than once")

    main = edit(main, "dim task start",
        '    xTaskCreatePinnedToCore(buzzer_task, "buzzer", 3072, nullptr, 1, nullptr, 0);\n',
        '    xTaskCreatePinnedToCore(buzzer_task, "buzzer", 3072, nullptr, 1, nullptr, 0);\n'
        '    xTaskCreatePinnedToCore(backlight_task, "backlight", 3072, nullptr, 1, nullptr, 0);\n')

    if not CONF.exists():
        failures.append("src/esp_panel_board_custom_conf.h not found")
    else:
        conf = CONF.read_text()
        mm = list(re.finditer(r'(#define ESP_PANEL_BOARD_USE_BACKLIGHT\s+)\((0|1)\)', conf))
        if len(mm) != 1:
            failures.append(f"USE_BACKLIGHT line: expected 1 match, found {len(mm)}")
        elif mm[0].group(2) == "0":
            conf_new = re.sub(r'(#define ESP_PANEL_BOARD_USE_BACKLIGHT\s+)\(0\)', r'\1(1)', conf)

# ---------------------------------------------------------------- ota: Wi-Fi updates
ota_header = None
if "ota" not in SKIP:
    main = edit(main, "ota include",
        "using namespace esp_panel::drivers;\n",
        '#include <ArduinoOTA.h>\n#include "ota_password.h"\nstatic bool otaStarted = false;\nusing namespace esp_panel::drivers;\n')
    main = edit(main, "ota loop",
        "void loop()\n{\n    wifi_service();\n",
        "void loop()\n{\n    wifi_service();\n    if (otaStarted) ArduinoOTA.handle();\n")
    main = edit(main, "ota begin",
        "            serverStarted = true;\n",
        r'''            serverStarted = true;
            ArduinoOTA.setHostname(DEVICE_HOST);
            ArduinoOTA.setPassword(OTA_PASSWORD);
            ArduinoOTA.setMdnsEnabled(false);   // main.cpp already runs mDNS
            ArduinoOTA.onStart([]() { Serial.println("OTA: update starting"); });
            ArduinoOTA.begin();
            otaStarted = true;
''')
    if not Path("src/ota_password.h").exists():
        ota_pw = secrets.token_urlsafe(9)
        ota_header = f'#pragma once\n#define OTA_PASSWORD "{ota_pw}"\n'

if failures:
    print("NOTHING was written. These edits did not match:")
    for f in failures:
        print("  -", f)
    print("Paste this output and I will adjust the script (or rerun with --skip=<feature>).")
    sys.exit(1)

MAIN.write_bytes((main.replace("\n", "\r\n") if crlf else main).encode("utf-8"))
if conf_new is not None:
    CONF.write_text(conf_new)
print("Done: src/main.cpp updated" + (" and backlight enabled in the board config." if conf_new else "."))

if "ota" not in SKIP:
    gi = Path(".gitignore")
    cur = gi.read_text() if gi.exists() else ""
    if "ota_password.h" not in cur:
        gi.write_text(cur + ("" if cur.endswith("\n") or not cur else "\n") + "src/ota_password.h\n")
    if ota_header:
        Path("src/ota_password.h").write_text(ota_header)
        pw = re.search(r'"(.*)"', ota_header).group(1)
        print(f"\nOTA password (also in src/ota_password.h, which is git-ignored): {pw}")
        print(f"  export PNOC_OTA={pw}")
    ini = Path("platformio.ini")
    t = ini.read_text() if ini.exists() else ""
    if "[env:viewe_2_8]" in t and "[env:viewe_2_8_ota]" not in t:
        ini.write_text(t.rstrip("\n") + "\n\n[env:viewe_2_8_ota]\nextends = env:viewe_2_8\n"
                       "upload_protocol = espota\nupload_port = pocketnoc.local\n"
                       "upload_flags = --auth=${sysenv.PNOC_OTA}\n")
        print("Added env viewe_2_8_ota to platformio.ini.")
    else:
        print("Could not add the OTA env to platformio.ini (env name differs or already there); tell me and I will adjust.")
print("\nNext (USB, one last time): pio run -t upload")
