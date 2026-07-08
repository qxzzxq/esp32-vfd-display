# VFD Clock — Design

A connected desk clock built on a Seeed XIAO ESP32-C3 driving a 16-character alphanumeric
grid VFD (built-in ASCII font, 3-wire SPI, existing `VFDDisplay` driver). A KY-040 rotary
encoder switches display pages and drives a settings menu. Indoor climate comes from a
local AHT20+BMP280 module; outdoor weather (incl. UV index) from the Open-Meteo API.
A small HTTP API lets other systems push a custom text page.

## Hardware / pin assignment (XIAO ESP32-C3)

Nothing beyond the VFD is wired yet — this table is the wiring guide.

| GPIO | XIAO | Use             | Notes |
|------|------|-----------------|-------|
| 2    | D0   | free            | Strap pin (must be high at boot) — deliberately unused |
| 3    | D1   | Encoder CLK (A) | KY-040 onboard pull-up, idles high |
| 4    | D2   | I2C SDA         | AHT20 @0x38, BMP280 @0x76 (probe 0x77 fallback), 100 kHz |
| 5    | D3   | I2C SCL         | Enable internal pull-ups as backstop; module pull-ups expected |
| 6    | D4   | VFD CS          | existing |
| 7    | D5   | VFD CLK         | existing |
| 21   | D6   | VFD DIN         | existing (U0TXD — UART0 console unusable → USB-Serial-JTAG console) |
| 20   | D7   | Encoder SW      | Internal pull-up; hold low ≥3 s at boot = provisioning recovery |
| 8    | D8   | free            | Strap pin — deliberately unused |
| 9    | D9   | free            | BOOT button — deliberately unused |
| 10   | D10  | Encoder DT (B)  | KY-040 onboard pull-up |

All three strap pins (2/8/9) stay unconnected. KY-040 runs at 3.3 V.

## Modules

All new code lives in `src/` (the only IDF component dir). `VFDDisplay.{h,cpp}` is unchanged.
Style: plain C++/C matching `VFDDisplay`, ESP-IDF C APIs, no frameworks. Producer modules
own their data and expose copy-out getters — no global state struct, no cross-module locking.

- **`encoder.{h,cpp}`** — KY-040 quadrature decode + button, GPIO-ISR based (ESP32-C3 has
  no PCNT peripheral). Rotation: full-step Gray-code transition table (4-bit prev<<2|curr
  lookup), emits a step only when the state returns to the detent — inherently rejects
  contact bounce, no timing debounce needed. Button: ANYEDGE ISR with 20 ms edge filter
  (`esp_timer_get_time()` delta), emits raw BtnDown/BtnUp. Events
  (`StepCW/StepCCW/BtnDown/BtnUp`) go to an 8-deep queue; UI blocks in
  `encoder_wait(ev, timeout)`.
- **`settings.{h,cpp}`** — NVS-backed `Settings` struct + static timezone table
  (~12–15 entries: display name ≤10 chars + POSIX TZ string). `settings_get()` (copy under
  mutex), `settings_save()` (diff-write keys, re-apply `setenv("TZ")+tzset()`).
- **`sensors.{h,cpp}`** — I2C master (`esp_driver_i2c`, `i2c_master.h`) + minimal AHT20 and
  BMP280 drivers. `sensors_read()` blocking ~90 ms, called only by the worker task;
  `sensors_get(&tC, &rh, &hPa)` returns false if never read / last read failed.
- **`weather.{h,cpp}`** — Open-Meteo fetch + cJSON parse. `weather_fetch(lat, lon)` blocking,
  worker task only; `weather_get(&Weather)` includes fetched-at timestamp for staleness.
- **`net.{h,cpp}`** — WiFi lifecycle + SNTP + worker task. STA mode: connect with backoff
  (1 s → 30 s cap), retry forever, no automatic fallback to portal. SNTP via `pool.ntp.org`
  once IP is up (RTC kept in UTC; TZ is display-only). `net_init(force_portal)`,
  `net_state()` (Portal/Connecting/Connected), `net_time_synced()`, `net_get_ip()`,
  `net_reset_credentials()` (erase ssid/pass, restart).
- **`web.{h,cpp}`** — one `esp_http_server`, two modes. Portal mode: form + `/save` +
  wildcard 302 + DNS-hijack task on 192.168.4.1; portal HTML as a raw string literal.
  API mode (STA): `/api/message`, `/api/status`; owns the RAM-only custom message
  (copy-out `web_get_message()`).
- **`ui.{h,cpp}`** — thin platform shell: owns the `VFDDisplay` instance and the encoder
  loop, builds the per-tick `UiSnapshot` from the producer modules, draws the line the
  core returns, and executes the returned `UiEffect`s. `ui_run()` never returns.
- **`ui/` (pure UI core)** — host-testable UI logic: `UiFsm` (Pages/Menu/Edit modes,
  click vs long-press recognition, menu timeout, overlay priority) plus the `UiPage` /
  `MenuItem` interfaces and their concrete singletons. Libc includes only — no
  ESP-IDF/FreeRTOS/project headers. Consumes a per-tick `UiSnapshot` built by the shell,
  returns the 16-char line + `UiEffect` list.
- **`main.cpp`** — boot: `nvs_flash_init` → `settings_init` → boot-hold check on GPIO20
  (3 s low → force portal) → `encoder_init` → `sensors_init` → `net_init(force)` → `ui_run()`.

## Task / concurrency model

- **UI task** = the `app_main` task. Loop: `encoder_wait(ev, 100 ms)`; event → input
  handling, timeout → re-render (clock seconds, edit-mode blink, marquee, auto-cycle).
  Only this task touches the VFD.
- **Worker task** (in `net.cpp`, prio 3, 8 KB stack — the in-task TLS handshake of
  `weather_fetch` peaks ~5 KB): 1 s loop; sensors every 10 s;
  weather every 15 min when Connected and lat/lon set (retry after 2 min on failure,
  max 3 retries per slot).
- **ISRs**: encoder rotation + button as above → queue (`xQueueSendFromISR`; drops when
  full are harmless).
- **IDF tasks**: wifi, lwIP, event loop, esp_timer, httpd. WiFi event handlers only set
  state and reconnect.
- **DNS hijack task**: AP mode only; answers every A query with 192.168.4.1.
- Sharing: one short-held mutex per producer module (settings, sensors, weather).

## UI

Every screen is 16 uppercase ASCII characters (VFD lowercase glyphs unverified).

**Pages** (CW = next, CCW = previous, wraps; auto-cycle skips empty CUSTOM; any input
pauses auto-cycle for 30 s):

| # | Page     | Layout (16 ch)       | Stale / unavailable |
|---|----------|----------------------|---------------------|
| 1 | TIME     | `    14:25:36    ` (12h: `   2:25:36 PM  `) | `    --:--:--    ` until SNTP sync |
| 2 | DATE     | ` SAT 2026-07-05 `   | ` NO TIME SYNC   ` |
| 3 | INDOOR   | `IN  23.4C   47% `   | `IN  SENSOR ERR  ` |
| 4 | OUTDOOR  | `OUT 31C 60% UV9 `   | `OUT NO DATA` / `SET LOCATION` if lat/lon empty; `?` suffix if >45 min old |
| 5 | CUSTOM   | POST'd text; ≤16 ch centered; >16 ch → marquee, 1 char / 300 ms, wraps through a 3-space gap | skipped in rotation when empty; auto-advances if cleared while shown; a non-empty POST jumps the display here (never interrupts the menu) |
| 6 | PRESSURE | `PRES 1013.2 hPa `   | page omitted if BMP280 probe failed (bonus page) |

**Encoder semantics — normal mode**: rotate = page switch. Long-press (≥1.0 s, fires
while held; `MENU     [==   ]` progress bar appears after 0.5 s) = enter menu. Click on
the pages is unassigned. Device status (IP address / `PORTAL 192.168.4.1` /
`WIFI CONNECTING`) is shown via the `>STATUS` menu item (M7).

**Menu**: rotate = move between items; click = enter edit (the `>` cursor moves to the
value side, e.g. ` BRIGHT      >12`); in edit rotate = change value, click = confirm →
immediate `settings_save` + live apply; 20 s inactivity or long-press = exit to pages
(abandons an uncommitted edit). Holding the button shows a progress bar after 0.5 s
(`EXIT     [==   ]`, 5 segments, one `=` per 100 ms render tick) and the exit fires at
1.0 s while still held.

```
>BRIGHT      13      0..15 → driver 0..240 (×16); clamp min 1 while editing; applied live
>24H         ON      ON/OFF
>TZ       PARIS      rotates timezone table
>CYCLE      10s      OFF/5/10/30/60 s
>WIFI RESET          click → "CLICK=CONFIRM" → click again → erase creds + reboot; rotate = cancel
>STATUS              click → IP address / WiFi state for 3 s
>EXIT
```

## HTTP API (STA mode, port 80)

No auth — trusted LAN, accepted risk (a static-token header check is a small add later).

- `POST /api/message` — body `{"text":"PIZZA AT 7PM"}`. Validation: `text` required,
  string, ≤64 chars, printable ASCII 0x20–0x7E. Empty string clears the page. Held in
  RAM only (owned by `web`) — cleared on reboot, replaced by next POST; deliberately
  not persisted (ephemeral by nature, avoids NVS wear). A non-empty POST switches the
  display to the CUSTOM page (notification semantics; the menu is never interrupted).
  Responses: `200 {"ok":true}`, `400 {"error":"..."}`, `413` if body >256 B.
- `GET /api/message` — `{"text":"..."}`.
- `GET /api/status` — `{"time":"2026-07-05T14:25:36","synced":true,`
  `"indoor":{"t":23.4,"rh":47.0,"p":1013.2},`
  `"weather":{"t":31.0,"rh":60.0,"uv":9.0,"age_s":312},"rssi":-58,"heap":123456}` —
  debugging aid for every milestone from M4 on.

## Provisioning

Hand-rolled portal (not the `wifi_provisioning` managed component — its SoftAP transport
speaks protobuf to Espressif's phone app, not a browser form; a portal is ~150 lines on
top of the httpd we already need).

```
boot → nvs_flash_init → settings_init
  → GPIO20 held low ≥3 s?  → erase creds → PORTAL
  → settings.ssid empty?   → PORTAL
  → else STA: connect, retry forever w/ backoff (1→30 s)
       got IP → web_start_api() + SNTP start
PORTAL: open AP "VFD-XXXX" (last 2 MAC bytes; short so it fits the 16-char
        status screen verbatim), IP 192.168.4.1,
        DNS hijack task, httpd with wildcard matching:
   GET  /      → HTML form: SSID, password, TZ <select>, lat, lon (optional)
   POST /save  → validate ssid non-empty → settings_save → "Rebooting…" → esp_restart
   GET  /*     → 302 → http://192.168.4.1/  (catches generate_204, hotspot-detect.html, …)
```

VFD during portal alternates `SETUP  VFD-XXXX` / `AP 192.168.4.1` every 3 s.
Deliberately **no** automatic STA→portal fallback (a router reboot must not demote the
clock to AP mode); recovery = menu WIFI RESET or boot-hold GPIO20.

## NVS schema

Namespace `vfdclk`:

| Key            | Type | Default  | Notes |
|----------------|------|----------|-------|
| `ssid` / `pass`| str  | ""       | empty ssid ⇒ portal |
| `bright`       | u8   | 200      | 0–240, driver units |
| `use24h`       | u8   | 1        | |
| `tz_idx`       | u8   | 0 (UTC)  | index into timezone table |
| `cycle_s`      | u8   | 0        | 0 = auto-cycle off |
| `lat` / `lon`  | str  | ""       | strings (NVS has no float); empty ⇒ weather disabled |

## Weather API

`GET https://api.open-meteo.com/v1/forecast?latitude={lat}&longitude={lon}&current=temperature_2m,relative_humidity_2m,uv_index`
— free, no API key, ~400 B JSON, parsed with cJSON. Fetched every 15 min. HTTPS via
`esp_http_client` + `esp_crt_bundle_attach`.
**Verified at M5** (2026-07-07, curl): `uv_index` is accepted in `current=` — the
`hourly=uv_index&forecast_days=1` fallback is not needed.

## Build config changes (at M0)

- `src/CMakeLists.txt` `PRIV_REQUIRES` add: `esp_driver_i2c esp_wifi esp_netif esp_event
  nvs_flash lwip esp_http_server esp_http_client esp-tls mbedtls json esp_timer`
- `sdkconfig.defaults` add:
  ```
  CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y        # UART0 pins consumed (VFD DIN=21, SW=20)
  CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y   # 1 MB factory app overflows w/ WiFi+TLS+httpd
  CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y
  CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN=y
  CONFIG_HTTPD_MAX_REQ_HDR_LEN=1024           # some browsers exceed 512 on the portal
  ```
  then delete generated `sdkconfig.seeed_xiao_esp32c3` (or fullclean) to regenerate.
- **Found at M0:** the PIO espidf builder generates `partitions.bin` from
  `board_build.partitions` in `platformio.ini` (default `partitions_singleapp.csv`) and
  ignores sdkconfig `CONFIG_PARTITION_TABLE_*` — so
  `board_build.partitions = partitions_singleapp_large.csv` is the authoritative setting;
  the sdkconfig option is kept only for consistency.
- Partition tradeoff: single-app-large = no OTA, USB reflash only — accepted for a desk
  clock; OTA later means a custom CSV with two ~1.6 MB slots (fits in 4 MB).

## Host unit tests

The pure UI core (`src/ui/`) is covered by Unity suites under `test/native/`, run on the
host with `pio test -e native` — no hardware needed. `[env:native]` compiles only
`src/ui/` into the test program (`build_src_filter`), so the rest of `src/` never touches
the host toolchain. The golden strings in the suites are transcribed from the
pre-refactor `ui.cpp`, pinning firmware-visible behavior across the UI refactor.

## Milestones (each hardware-verifiable)

- **M0 — Build plumbing** ✅ (2026-07-05): PRIV_REQUIRES + sdkconfig.defaults +
  `board_build.partitions`; demo still runs.
  *Verified:* boot logs over USB-Serial-JTAG; factory slot 0x177000; app ~192 KB.
- **M1 — Encoder** ✅ (2026-07-05): log events from a stub loop.
  *Verified on hardware:* one event per detent, click vs long-press distinguishable;
  direction sign was inverted for this wiring and is flipped at the emission point in
  `encoder.cpp` (physical CW ⇒ accum −4 ⇒ StepCW).
- **M2 — Settings + UI skeleton** ✅ (2026-07-05): TIME/DATE pages (placeholder
  `--:--:--`), brightness menu item end-to-end.
  *Verified on hardware:* pages switch; menu/edit/hold gestures work; brightness
  persists across reboot.
- **M3 — Sensors** ✅ (2026-07-05): INDOOR (+PRESSURE) pages.
  *Verified on hardware:* plausible values (BMP280 found at 0x77; 842 hPa station
  pressure cross-checked against ABQ ~1500 m elevation + sea-level report); breathe
  test → RH rises. Not yet exercised: SDA-unplug recovery (lazy re-init code path).
  Open question: AHT20 reads ~+1 °C vs room reference — likely heat from the
  VFD/XIAO nearby; add a calibration offset setting if it persists (M7).
- **M4 — WiFi + provisioning + SNTP** ✅ (2026-07-06): portal mode + boot-hold recovery.
  *Verified on hardware:* captive portal provisioning from a phone (AP `VFD-1111`),
  joins home WiFi, time live in configured TZ; menu WIFI RESET returns to portal.
  Not yet exercised: boot-hold (hold SW ≥3 s at power-on) recovery path.
- **M5 — Weather** ✅ (2026-07-07): OUTDOOR page + `weather` module + worker cadence.
  *Verified on hardware:* live OUTDOOR readings; router unplug → stale `?` marker
  appears. Not yet exercised: recovery on reconnect (expect fresh values and the
  `?` gone within ~15 min of the router returning); values not formally
  cross-checked against open-meteo.com.
- **M6 — HTTP API + custom page** ✅ (2026-07-07): `/api/message` (POST/GET) +
  `/api/status` + CUSTOM page with centered/marquee rendering.
  *Verified on hardware (curl against the live device):* POST shows the page and
  round-trips through GET; 64-char text accepted, 65 → 400; missing/non-string
  text, bad JSON, and ``-smuggled control chars → 400; >256 B body → 413;
  `"`/`\` escaping round-trips; `/api/status` sane (synced local time, plausible
  indoor/weather, negative RSSI); heap stable across 60 requests. The message is
  RAM-only by design (cleared on reboot — no NVS wear). Not yet exercised by eye:
  marquee cadence/wrap gap on the VFD, auto-advance when the shown message is
  cleared.
- **M7 — Polish**: full menu (24H, TZ, CYCLE, STATUS), auto-cycle, portal
  lat/lon/TZ fields. *Verify:* overnight soak — no reboots, clock correct, heap stable
  (via /api/status).

## Open questions / risks

1. App size vs 1.5 MB factory slot — expected to fit with the common-CA cert bundle
   subset; if not, custom partition CSV (check at M0).
2. `esp_netif_sntp.h` component home (esp_netif vs lwip) — both in PRIV_REQUIRES,
   self-resolving at first build.
3. ~~Open-Meteo `current=uv_index` support~~ — verified working at M5 (2026-07-07);
   no hourly fallback needed.
4. VFD lowercase glyphs unverified — design is uppercase-only.
5. No automatic portal fallback after repeated STA failures — deliberate; revisit if it
   annoys in practice.
6. Sensor-module I2C pull-ups assumed present — internal pull-ups as backstop; verify at M3.
7. Provisioning AP is open (passwordless) — brief exposure during setup only; accepted.
