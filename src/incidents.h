#pragma once
#include <Arduino.h>

void incidents_begin();
void incidents_start(const char *name);   // service confirmed down
void incidents_end(const char *name);     // service recovered
void incidents_close_missing(const char *const *names, int count);
uint32_t incidents_version();             // changes whenever the log changes
void incidents_format(char *out, size_t len);
