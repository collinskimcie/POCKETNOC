#!/usr/bin/env python3
"""PocketNOC round 3a: more check types per service.

    Name, https://url                      web check (as before)
    Name, https://url, contains=word       web check, the page must contain the word
    Name, tcp://host:port                  TCP port check
    Name, ping://host                      ICMP ping check

Run from the project root (needs rounds 1 and 2 applied):
    python3 apply_round3a.py

Edits src/main.cpp and src/nettools.cpp. Every edit must match exactly once;
if any edit does not match, NOTHING is written.
Undo with `git checkout src/main.cpp src/nettools.cpp`.
"""
import re
import sys
from pathlib import Path

MAIN = Path("src/main.cpp")
NET = Path("src/nettools.cpp")
for p in (MAIN, NET):
    if not p.exists():
        sys.exit(f"{p} not found. Run this from the project root.")


def load(p):
    raw = p.read_bytes().decode("utf-8")
    return raw.replace("\r\n", "\n"), "\r\n" in raw


def save(p, text, crlf):
    p.write_bytes((text.replace("\n", "\r\n") if crlf else text).encode("utf-8"))


main, main_crlf = load(MAIN)
net, net_crlf = load(NET)

if "body_contains" in main:
    sys.exit("Round 3a looks applied already (body_contains is in src/main.cpp).")
if "CHECK_SKIPPED" not in main:
    sys.exit("Round 1 (memory guard) is not in src/main.cpp. Apply round 1 first.")

failures = []


def edit(text, label, old, new, regex=False):
    if regex:
        found = list(re.finditer(old, text, re.S))
        if len(found) != 1:
            failures.append(f"{label}: expected 1 match, found {len(found)}")
            return text
        m = found[0]
        return text[: m.start()] + new + text[m.end():]
    n = text.count(old)
    if n != 1:
        failures.append(f"{label}: expected 1 match, found {n}")
        return text
    return text.replace(old, new)


# ------------------------------------------------------------ main.cpp
# 1. a service can now carry a keyword
main = edit(main, "Service struct",
            "struct Service { char name[24]; char url[110]; };",
            "struct Service { char name[24]; char url[110]; char word[24]; };")

# 2. config parsing: new schemes and the optional contains= field
main = edit(main, "parse_config",
            r'// Format: one service per line, "Name, https://url"\. Returns the number parsed\.\n.*?\n\}\n',
            r'''// Format: one service per line:  "Name, https://url"  "Name, tcp://host:port"  "Name, ping://host"
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
        url.trim();
        if (name.length() == 0 || !url_ok(url)) continue;
        if (!(url.startsWith("http://") || url.startsWith("https://"))) word = "";   // keywords: web checks only
        strlcpy(out[n].name, name.c_str(), sizeof(out[n].name));
        strlcpy(out[n].url, url.c_str(), sizeof(out[n].url));
        strlcpy(out[n].word, word.c_str(), sizeof(out[n].word));
        n++;
    }
    return n;
}
''', regex=True)

# 3. help text on the web page
main = edit(main, "services help text",
            '"<p>One service per line: <code>Name, https://url</code> (max 5)</p>"',
            '"<p>One service per line (max 5): <code>Name, https://url</code>, or '
            '<code>Name, tcp://host:port</code> (port check), or <code>Name, ping://host</code>. '
            'Add <code>, contains=word</code> to a web line to require that word in the page '
            '(case sensitive, up to 23 characters, searched in the first 16 KB).</p>"')

# 4. the checks themselves
main = edit(main, "check functions",
            r"// One request\. Returns the HTTP status code.*?return lowHeap \? CHECK_SKIPPED : code;\n\}\n",
            r'''// ---- Service checks ----
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
    int colon = hp.lastIndexOf(':');
    if (colon < 1) return -100;
    String host = hp.substring(0, colon);
    int port = hp.substring(colon + 1).toInt();
    if (port < 1 || port > 65535) return -100;
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

static int check_service(const Service &s, uint32_t &ms)
{
    if (strncmp(s.url, "tcp://", 6) == 0) return check_tcp(s.url + 6, ms);
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
''', regex=True)

# 5. the call in the monitor task
main = edit(main, "monitor call",
            "            int code = check_service(snap[i].url, ms);\n",
            "            int code = check_service(snap[i], ms);\n")

# ------------------------------------------------------------ nettools.cpp
net = edit(net, "nettools_ping",
           "bool nettools_is_busy() { return s_busy; }\n",
           r'''bool nettools_is_busy() { return s_busy; }

// Used by main.cpp for ping:// service checks. Returns 1 = reply, 0 = no reply, -1 = a tool is using the ICMP helper.
int nettools_ping(const char *host, uint32_t timeout_ms, uint32_t &rtt)
{
    if (s_busy) return -1;
    IPAddress ip;
    if (!resolve(String(host), ip)) return 0;
    return ping_once(ip, timeout_ms, rtt) ? 1 : 0;
}
''')

if failures:
    print("NOTHING was written. These edits did not match:")
    for f in failures:
        print("  -", f)
    print("Send me the lines around each one and I will adjust the script.")
    sys.exit(1)

save(MAIN, main, main_crlf)
save(NET, net, net_crlf)
print("Done: src/main.cpp and src/nettools.cpp updated.")
print("Next: pio run && pio run -t upload   (undo: git checkout src/main.cpp src/nettools.cpp)")
