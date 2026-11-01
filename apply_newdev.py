#!/usr/bin/env python3
"""PocketNOC: 'New devices' and 'Forget devices' tools in src/nettools.cpp.
Every edit must match exactly once, or NOTHING is written.  Undo: git checkout src/nettools.cpp
"""
import sys
from pathlib import Path

P = Path("src/nettools.cpp")
if not P.exists():
    sys.exit("src/nettools.cpp not found. Run this from the project root.")
raw = P.read_bytes().decode("utf-8")
crlf = "\r\n" in raw
s = raw.replace("\r\n", "\n")
if "tool_newdev" in s:
    sys.exit("Already applied (tool_newdev is in src/nettools.cpp).")

CODE = r'''// ---------- New devices: remember which MAC addresses are normal on this LAN ----------
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

'''

def edit(text, label, old, new):
    n = text.count(old)
    if n != 1:
        print(f"{label}: expected 1 match, found {n}")
        print("NOTHING was written. Paste this output and I will adjust the script.")
        sys.exit(1)
    return text.replace(old, new)

s = edit(s, "tool table", "static const ToolDef TOOLS[] = {\n", CODE + "static const ToolDef TOOLS[] = {\n")
s = edit(s, "tool entries", '    {"LAN", "Wake-on-LAN", tool_wol, 0, false,',
         '    {"LAN", "New devices", tool_newdev, 0, false,\n'
         '     "Scan the network and flag unknown MAC addresses"},\n'
         '    {"LAN", "Forget devices", tool_forgetdev, 0, false,\n'
         '     "Clear the known-devices list"},\n'
         '    {"LAN", "Wake-on-LAN", tool_wol, 0, false,')

P.write_bytes((s.replace("\n", "\r\n") if crlf else s).encode("utf-8"))
print("Done: src/nettools.cpp updated.")
print("Next: pio run -t upload   (undo: git checkout src/nettools.cpp)")
