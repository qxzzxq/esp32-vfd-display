# esp32-vfd — VFD Desk Clock

Connected desk clock: Seeed XIAO ESP32-C3 + 16-char alphanumeric grid VFD (built-in ASCII
font, 3-wire SPI) + KY-040 rotary encoder + AHT20/BMP280 (indoor) + Open-Meteo (outdoor).
PlatformIO with **ESP-IDF 5.5** — NOT Arduino. Full design, UI spec, API contract, and
milestone roadmap: `docs/DESIGN.md`.

## Build / flash / monitor

The `pio` CLI is not on PATH:

```
~/.platformio/penv/bin/pio run -e seeed_xiao_esp32c3            # build
~/.platformio/penv/bin/pio run -e seeed_xiao_esp32c3 -t upload  # flash (esp-builtin USB-JTAG)
~/.platformio/penv/bin/pio device monitor                       # serial monitor
~/.platformio/penv/bin/pio run -t menuconfig                    # IDF menuconfig
```

- Console is **USB-Serial-JTAG, not UART0** (GPIO21/20 are repurposed as VFD DIN / encoder SW).
- After editing `sdkconfig.defaults`, delete the generated `sdkconfig.seeed_xiao_esp32c3`
  (or `-t fullclean`) so it regenerates.
- Do not remove the `platform_packages` symlink in `platformio.ini` — it works around the
  platform's metadata-only `tool-openocd-esp32` package (debug breaks without it).

## Pin map (XIAO ESP32-C3)

| GPIO | Use | | GPIO | Use |
|---|---|---|---|---|
| 3 | Encoder CLK (A) | | 6 | VFD CS |
| 10 | Encoder DT (B) | | 7 | VFD CLK |
| 20 | Encoder SW (boot-hold ≥3 s = WiFi re-provision) | | 21 | VFD DIN |
| 4 | I2C SDA (AHT20 0x38, BMP280 0x76/0x77) | | 5 | I2C SCL |

Strap pins GPIO2/8/9 are deliberately unused — do not assign them.

## Architecture

Modules in `src/` (see `docs/DESIGN.md` for APIs): `encoder` (GPIO-ISR quadrature — the
C3 has no PCNT), `settings` (NVS namespace `vfdclk` + timezone table), `sensors` (I2C
AHT20/BMP280), `weather` (Open-Meteo + cJSON), `net` (WiFi STA/portal, SNTP, worker task),
`web` (one esp_http_server: captive portal or `/api/*`), `ui` (pages + menu state machine),
plus the existing `VFDDisplay` driver (unchanged).

Concurrency: UI task (= app_main) blocks on the encoder queue with a 100 ms render tick and
is the **only** task that touches the VFD; a worker task in `net.cpp` polls sensors (10 s)
and weather (15 min). Every screen is 16 uppercase ASCII chars.

## Conventions

- `src/` is the ONLY IDF component dir (PlatformIO sets EXTRA_COMPONENT_DIRS to just it).
  New IDF subsystems must be added to `PRIV_REQUIRES` in `src/CMakeLists.txt`; new source
  files are picked up automatically (GLOB_RECURSE).
- Plain C++ classes / ESP-IDF C APIs, no frameworks. Producer modules own their data and
  expose copy-out getters. Match the style of `VFDDisplay.{h,cpp}`.
- clangd/IDE shows false "file not found" errors for IDF headers until a build regenerates
  `compile_commands.json` — trust the pio build.

## Gotchas

- Partition table: single-app-large (1.5 MB factory app), no OTA — reflash over USB only.
  It is selected by `board_build.partitions` in `platformio.ini` — the PIO espidf builder
  IGNORES sdkconfig `CONFIG_PARTITION_TABLE_*` (we keep the sdkconfig option set anyway so
  both layers agree).
- Time is lost on power cycle (no battery RTC) until SNTP resyncs.
