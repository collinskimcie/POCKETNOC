# PocketNOC

A pocket-sized network operations center for the Viewe 2.8" ESP32-S3
touchscreen (UEDX24320028E-WB-A). It watches your web services, shows their
health at a glance, and doubles as a handheld network toolkit.

Built with PlatformIO, the Arduino framework and LVGL 8.4.

## Features

- **Home dashboard**: ring gauge of services up, plain-language status,
  clock, Wi-Fi strength, average response time, uptime, and quick buttons
  (Scan, Ping, Speed, Check).
- **Service monitor**: periodic HTTP(S) checks, per-service detail page with
  response-time chart and uptime. A red banner appears when a service is down.
- **Tools**: Wi-Fi scan, ping, speed test, latency test, network info,
  signal meter.
- **Clock** (analog and digital, NTP) and **Info** page (Wi-Fi, IP, heap,
  uptime, build date).
- **Web config page** at `http://pocketnoc.local/` to manage services and
  Wi-Fi networks.
- Returns to Home after 90 s without a touch.

## Build and flash

1. Install [PlatformIO](https://platformio.org/).
2. Copy the secrets template and fill in your values:

       cp src/secrets.example.h src/secrets.h

   `src/secrets.h` is git-ignored and never leaves your machine.
3. Build, flash and open the serial monitor:

       pio run -t upload && pio device monitor

## First-time Wi-Fi setup

If no saved network can be joined, the device starts its own access point
(`<DEVICE_NAME>-Setup`). Join it, open `192.168.4.1`, and add your Wi-Fi.
After that, use `http://pocketnoc.local/`.

## Configuration

- Time zone: `CLOCK_TZ` in `src/main.cpp` (default `EAT-3`, UTC+3).
- Theme: `UI_DARK` in `src/main.cpp`.

## Status

Work in progress. Planned: incident history and phone alerts
(Telegram or webhook).
