#!/usr/bin/env python3
"""PocketNOC: scheduled new-device scan (nettools.cpp function + monitor task hook).
Every edit must match exactly once, or NOTHING is written.  Undo: git checkout src/main.cpp src/nettools.cpp
"""
import sys
from pathlib import Path

MAIN, NET = Path("src/main.cpp"), Path("src/nettools.cpp")
for p in (MAIN, NET):
    if not p.exists():
        sys.exit(f"{p} not found. Run this from the project root.")

def load(p):
    r = p.read_bytes().decode("utf-8")
    return r.replace("\r\n", "\n"), "\r\n" in r

main, mc = load(MAIN)
net, nc = load(NET)
if "nettools_scan_new" in main or "nettools_scan_new" in net:
    sys.exit("Already applied.")
if "arp_mac" not in net:
    sys.exit("The New devices tool (apply_newdev.py) is not in nettools.cpp yet. Apply it first.")

fail = []
def edit(text, label, old, new):
    n = text.count(old)
    if n != 1:
        fail.append(f"{label}: expected 1 match, found {n}")
        return text
    return text.replace(old, new)

FUNC = r'''// Used by main.cpp for the scheduled scan. Returns the number of devices that are not in the saved
// baseline, -1 if a tool is busy or Wi-Fi is down, -2 if no baseline exists yet.
int nettools_scan_new(char *first, size_t firstLen)
{
    if (s_busy || WiFi.status() != WL_CONNECTED) return -1;
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
    if (knownN == 0) return -2;
    s_busy = true;
    if (first && firstLen) first[0] = 0;
    IPAddress me = WiFi.localIP();
    int fresh = 0;
    for (int h = 1; h <= 254; h++) {
        if (h == me[3]) continue;
        if (WiFi.status() != WL_CONNECTED) break;
        IPAddress ip(me[0], me[1], me[2], h);
        uint32_t rtt = 0;
        ping_once(ip, 150, rtt);
        uint8_t mac[6];
        if (!arp_mac(ip, mac)) continue;
        bool isKnown = false;
        for (int k = 0; k < knownN; k++) if (memcmp(known + k * 6, mac, 6) == 0) { isKnown = true; break; }
        if (isKnown) continue;
        fresh++;
        if (first && firstLen && !first[0]) {
            snprintf(first, firstLen, "%s %02X:%02X:%02X:%02X:%02X:%02X", ip.toString().c_str(),
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
    }
    s_busy = false;
    return fresh;
}

'''
net = edit(net, "scan function", "static const ToolDef TOOLS[] = {\n", FUNC + "static const ToolDef TOOLS[] = {\n")

main = edit(main, "prototype",
    "int nettools_ping(const char *host, uint32_t timeout_ms, uint32_t &rtt);   // nettools.cpp\n",
    "int nettools_ping(const char *host, uint32_t timeout_ms, uint32_t &rtt);   // nettools.cpp\n"
    "int nettools_scan_new(char *first, size_t firstLen);                         // nettools.cpp\n")

HOOK = r'''        {   // scheduled new-device scan: first 10 minutes after boot, then every 6 hours
            static uint32_t nextScan = 0;
            static int lastFresh = 0;
            if (nextScan == 0) nextScan = millis() + 10UL * 60UL * 1000UL;
            if ((int32_t)(millis() - nextScan) >= 0) {
                nextScan = millis() + 6UL * 3600UL * 1000UL;
                char who[48];
                int f = nettools_scan_new(who, sizeof(who));
                if (f > 0 && f > lastFresh) {
                    char m[96];
                    snprintf(m, sizeof(m), "%d unknown device(s) on the network, e.g. %s", f, who);
                    notify_queue(m, NOTE_TEST);
                }
                if (f >= 0) lastFresh = f;
                else if (f == -1) nextScan = millis() + 10UL * 60UL * 1000UL;   // a tool is busy: try again soon
            }
        }
'''
main = edit(main, "monitor hook",
    "        notify_flush();\n\n        // Wait for the next round, but wake immediately if the config changes.\n",
    HOOK + "        notify_flush();\n\n        // Wait for the next round, but wake immediately if the config changes.\n")

if fail:
    print("NOTHING was written. These edits did not match:")
    for f in fail:
        print("  -", f)
    print("Paste this output and I will adjust the script.")
    sys.exit(1)

MAIN.write_bytes((main.replace("\n", "\r\n") if mc else main).encode("utf-8"))
NET.write_bytes((net.replace("\n", "\r\n") if nc else net).encode("utf-8"))
print("Done: src/main.cpp and src/nettools.cpp updated.")
print("Next: pio run -t upload")
