#!/usr/bin/env python3
"""PocketNOC: 'Traceroute' tool in src/nettools.cpp (own raw ICMP socket).
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
if "tool_trace" in s:
    sys.exit("Already applied (tool_trace is in src/nettools.cpp).")

CODE = r'''// ---------- Traceroute: ICMP echo with a growing hop limit, on a raw socket ----------
#include "lwip/sockets.h"

#define TRACE_TARGET "1.1.1.1"      // change to trace somewhere else
static const int TRACE_MAX_HOPS = 16;

static void tool_trace()
{
    if (WiFi.status() != WL_CONNECTED) { out_set("Wi-Fi not connected"); return; }
    IPAddress target;
    if (!resolve(String(TRACE_TARGET), target)) { out_set("Cannot resolve " TRACE_TARGET); return; }
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) { out_set("Raw ICMP socket not available\nin this firmware build."); return; }
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = (uint32_t)target;

    uint16_t id = (uint16_t)(esp_random() & 0xFFFF);
    String out = String("Traceroute to ") + target.toString() + "\n\n";
    out_set(out + "Starting...");
    bool reached = false;
    for (int ttl = 1; ttl <= TRACE_MAX_HOPS && !cancelled() && !reached; ttl++) {
        int t = ttl;
        setsockopt(sock, IPPROTO_IP, IP_TTL, &t, sizeof(t));
        uint8_t pkt[16];
        memset(pkt, 0, sizeof(pkt));
        pkt[0] = 8;                       // echo request
        pkt[4] = id >> 8;  pkt[5] = id & 0xFF;
        pkt[6] = 0;        pkt[7] = (uint8_t)ttl;
        uint32_t sum = 0;
        for (int i = 0; i < 16; i += 2) sum += ((uint32_t)pkt[i] << 8) | pkt[i + 1];
        while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
        uint16_t ck = (uint16_t)(~sum & 0xFFFF);
        pkt[2] = ck >> 8;  pkt[3] = ck & 0xFF;

        uint32_t t0 = millis();
        sendto(sock, pkt, sizeof(pkt), 0, (struct sockaddr *)&dst, sizeof(dst));
        char line[48];
        bool got = false;
        while (millis() - t0 < 1000 && !cancelled()) {
            uint8_t buf[128];
            struct sockaddr_in from;
            socklen_t fl = sizeof(from);
            int r = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
            if (r < 0) break;              // receive timeout
            if (r < 28) continue;
            int hl = (buf[0] & 0x0F) * 4;
            if (hl < 20 || hl + 8 > r) continue;
            const uint8_t *ic = buf + hl;
            const uint8_t *eid = nullptr;  // where our echo id/seq are, in either answer type
            if (ic[0] == 0) {              // echo reply: the target itself answered
                eid = ic + 4;
                reached = true;
            } else if (ic[0] == 11) {      // time exceeded: a router on the way answered
                const uint8_t *in = ic + 8;                 // our original IP header
                if (in + 20 > buf + r) continue;
                int ihl = (in[0] & 0x0F) * 4;
                if (ihl < 20 || in + ihl + 8 > buf + r) continue;
                eid = in + ihl + 4;
            } else {
                continue;
            }
            if (((eid[0] << 8) | eid[1]) != id || eid[3] != (uint8_t)ttl) { reached = false; continue; }
            IPAddress who((uint32_t)from.sin_addr.s_addr);
            snprintf(line, sizeof(line), "%2d  %s  %lu ms\n", ttl, who.toString().c_str(), (unsigned long)(millis() - t0));
            out += line;
            got = true;
            break;
        }
        if (!got) {
            snprintf(line, sizeof(line), "%2d  *\n", ttl);
            out += line;
        }
        out_set(out);
    }
    close(sock);
    if (cancelled()) return;
    out += reached ? "\nReached the target." : "\nTarget not reached. Some routers\nnever answer (shown as *).";
    out_set(out);
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
s = edit(s, "tool entry", '    {"LAN", "New devices", tool_newdev, 0, false,',
         '    {"Internet", "Traceroute", tool_trace, 0, false,\n'
         '     "Show the routers between the board and 1.1.1.1"},\n'
         '    {"LAN", "New devices", tool_newdev, 0, false,')

P.write_bytes((s.replace("\n", "\r\n") if crlf else s).encode("utf-8"))
print("Done: src/nettools.cpp updated.")
print("Next: pio run -t upload   (undo: git checkout src/nettools.cpp)")
