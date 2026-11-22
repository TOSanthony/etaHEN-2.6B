# etaHEN NTP Time Sync Plugin

<p align="center">
  <img src="assets/logo.png" alt="etaHEN" height="128" width="128"/>
</p>

<p align="center">
  <b>etaHEN NTP Plugin</b><br/>
  Network Time Protocol system clock synchronization for PlayStation 5
</p>

<p align="center">
  ·
  <b>English</b>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="license"/></a>
  <img src="https://img.shields.io/badge/Platform-PlayStation%205-003791?style=flat&logo=playstation" alt="PlayStation 5"/>
  <img src="https://img.shields.io/badge/C-00599C?style=flat&logo=c&logoColor=white" alt="C"/>
  <img src="https://img.shields.io/badge/Build-CMake-064F8C?style=flat&logo=cmake" alt="CMake"/>
</p>

This repository contains the standalone NTP clock synchronization plugin for etaHEN on PlayStation 5. It automatically synchronizes the system date and time using public NTP servers, which is particularly beneficial for consoles whose CMOS/RTC battery is depleted or disconnected.

## Features

- **Automatic Clock Synchronization**: Synchronizes system clock on boot and periodically according to configured intervals.
- **European & Global NTP Pools**: Built-in presets for the France pool (`fr.pool.ntp.org`, default), Europe pool, Germany pool, PTB Germany (`ptbtime1.ptb.de`), Global Pool, Cloudflare and Google.
- **Multi-Tier Fallback**: Tries the selected server first, then falls back to the other European pools and PTB, then to global providers.
- **Latency Compensation**: Calculates network round-trip time (RTT) and compensates for half-RTT transit delay.
- **Dynamic Bilingual i18N**: Automatically tracks PS5 system language changes (`zh-Hans` / `en`) in real-time without needing a console or plugin restart.
- **Direct & Interactive etaHEN UI**: Flat root page layout in etaHEN ShellUI without nested sub-page clicks, featuring a "Sync Time Now" action with network confirmation alert.
- **On-Screen Notifications**: Shows localized popup notifications reporting sync progress and results.
- **Persistent Configuration**: Stores preferences in `/data/etaHEN/plugins/NTPS00001.ini`.

## Requirements

- [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk)
- CMake 3.20 or newer
- Ninja
- Python 3.9 or newer

## Build

```sh
export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
cmake --preset ps5
cmake --build --preset ps5
```

The resulting executable will be `build-ps5/bin/ntp.elf`.

## Installation

Upload the built ELF to your PS5 plugin directory:

```text
/data/etaHEN/plugins/NTPS00001.elf
```

To perform an atomic update:
1. Upload to `/data/etaHEN/plugins/NTPS00001.installing`
2. Rename to `/data/etaHEN/plugins/NTPS00001.elf`

etaHEN will automatically detect and run the plugin.

## Project Structure

```text
.
├── cmake/ps5-toolchain.cmake     PS5 toolchain file
├── include/
│   ├── ntp_i18n.h                Bilingual internationalization and system language tracking
│   ├── ntp_service.h             Background service and worker thread interface
│   ├── ntp_sync.h                NTP client and clock synchronization engine
│   ├── plugin_config.h.in        Generated plugin configuration and metadata
│   ├── plugin_settings.h         Persistent configuration structure and loader
│   └── plugin_ui.h               etaHEN dynamic UI definitions
├── source/
│   ├── main.c                    Process entry, privilege elevation, IPC loop, language polling
│   ├── ntp_i18n.c                System language query (SceSystemService) & translation tables
│   ├── ntp_service.c             Worker thread, periodic scheduler, sync callback
│   ├── ntp_sync.c                NTP protocol, SceNet session, multi-tier fallback, settimeofday
│   ├── plugin_descriptor.c       Embedded ELF plugin descriptor (NTPS00001)
│   ├── plugin_settings.c         INI parser and atomic file saver
│   └── plugin_ui.c               UI document nodes and event dispatching
├── CMakeLists.txt                Project build definitions
└── CMakePresets.json             Standard presets for PS5 builds
```

## Acknowledgements

- Based on [ps5-date-time-sync](https://github.com/kerrdec97/ps5-date-time-sync) by Deckerr97.
- Powered by [etaHEN](https://github.com/aydencharles/etaHEN) and [etaHEN Plugin SDK](../sdk).
- Thanks to John Törnblom and contributors to the [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk).

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE).
