#include "incidents.h"
#include <Preferences.h>
#include <time.h>

static const int MAX_INC = 12;
static const time_t TIME_VALID_AFTER = 1700000000;   // earlier means NTP has not synced yet

enum { INC_OPEN = 0, INC_CLOSED = 1, INC_END_UNKNOWN = 2 };

struct Incident {
    char name[20];
    uint32_t start;   // epoch seconds, 0 = clock was not synced
    uint32_t end;
    uint8_t state;
};

static Incident inc[MAX_INC];
static int n_inc = 0;
static uint32_t version = 1;
static SemaphoreHandle_t mtx = nullptr;

static uint32_t now_epoch()
{
    time_t t = time(nullptr);
    return t > TIME_VALID_AFTER ? (uint32_t)t : 0;
}

static void save()
{
    Preferences p;
    p.begin("incidents", false);
    p.putUChar("n", (uint8_t)n_inc);
    p.putBytes("log", inc, sizeof(inc));
    p.end();
    version++;
}

void incidents_begin()
{
    mtx = xSemaphoreCreateMutex();
    Preferences p;
    p.begin("incidents", false);
    if (p.isKey("log") && p.getBytesLength("log") == sizeof(inc)) {
        p.getBytes("log", inc, sizeof(inc));
        n_inc = p.getUChar("n", 0);
        if (n_inc < 0 || n_inc > MAX_INC) n_inc = 0;
    }
    p.end();
    // anything still open at boot was cut off by the reboot
    bool dirty = false;
    for (int i = 0; i < n_inc; i++) {
        if (inc[i].state == INC_OPEN) { inc[i].state = INC_END_UNKNOWN; dirty = true; }
    }
    if (dirty) save();
}

static bool same_name(const Incident &r, const char *name)
{
    return strncmp(r.name, name, sizeof(r.name) - 1) == 0;
}

void incidents_start(const char *name)
{
    xSemaphoreTake(mtx, portMAX_DELAY);
    bool already = false;
    for (int i = 0; i < n_inc; i++) {
        if (inc[i].state == INC_OPEN && same_name(inc[i], name)) already = true;
    }
    if (!already) {
        if (n_inc == MAX_INC) {
            memmove(&inc[0], &inc[1], sizeof(Incident) * (MAX_INC - 1));
            n_inc--;
        }
        Incident &r = inc[n_inc++];
        memset(&r, 0, sizeof(r));
        strncpy(r.name, name, sizeof(r.name) - 1);
        r.start = now_epoch();
        r.state = INC_OPEN;
        save();
    }
    xSemaphoreGive(mtx);
}

void incidents_end(const char *name)
{
    xSemaphoreTake(mtx, portMAX_DELAY);
    for (int i = n_inc - 1; i >= 0; i--) {
        if (inc[i].state == INC_OPEN && same_name(inc[i], name)) {
            inc[i].end = now_epoch();
            inc[i].state = INC_CLOSED;
            save();
            break;
        }
    }
    xSemaphoreGive(mtx);
}

// After a config edit: close incidents of services that no longer exist.
void incidents_close_missing(const char *const *names, int count)
{
    xSemaphoreTake(mtx, portMAX_DELAY);
    bool dirty = false;
    for (int i = 0; i < n_inc; i++) {
        if (inc[i].state != INC_OPEN) continue;
        bool found = false;
        for (int k = 0; k < count; k++) {
            if (same_name(inc[i], names[k])) found = true;
        }
        if (!found) { inc[i].state = INC_END_UNKNOWN; dirty = true; }
    }
    if (dirty) save();
    xSemaphoreGive(mtx);
}

uint32_t incidents_version() { return version; }

static void fmt_duration(char *out, size_t len, uint32_t s)
{
    if (s < 60) snprintf(out, len, "%us", (unsigned)s);
    else if (s < 3600) snprintf(out, len, "%um %us", (unsigned)(s / 60), (unsigned)(s % 60));
    else snprintf(out, len, "%uh %um", (unsigned)(s / 3600), (unsigned)((s % 3600) / 60));
}

void incidents_format(char *out, size_t len)
{
    xSemaphoreTake(mtx, portMAX_DELAY);
    out[0] = 0;
    if (n_inc == 0) {
        snprintf(out, len, "No incidents yet.\n\nA service that stays down for several checks in a row is logged here.");
    }
    size_t used = 0;
    for (int i = n_inc - 1; i >= 0; i--) {
        const Incident &r = inc[i];
        char when[24] = "time n/a";
        if (r.start) {
            time_t t = r.start;
            struct tm ti;
            localtime_r(&t, &ti);
            strftime(when, sizeof(when), "%d %b %H:%M", &ti);
        }
        char dur[24];
        if (r.state == INC_OPEN) snprintf(dur, sizeof(dur), "still down");
        else if (r.state == INC_END_UNKNOWN) snprintf(dur, sizeof(dur), "end unknown");
        else if (r.start && r.end >= r.start) fmt_duration(dur, sizeof(dur), r.end - r.start);
        else snprintf(dur, sizeof(dur), "recovered");
        int w = snprintf(out + used, len - used, "%s\n  %s, %s\n", r.name, when, dur);
        if (w < 0 || (size_t)w >= len - used) break;
        used += w;
    }
    xSemaphoreGive(mtx);
}
