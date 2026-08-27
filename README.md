# AstroPlate

A tiny "is tonight worth the telescope?" display for the
[Soldered Inkplate 2](https://soldered.com/product/inkplate-2/) (2.13"
black/white/red e-paper, ESP32). It wakes up periodically, fetches tonight's
astronomical forecast, computes the Moon and planets locally, draws a
**GO / MAYBE / NO GO** verdict, and goes back to deep sleep.

Companion project to [stockplate](https://github.com/awailly/stockplate)
(Inkplate 2 stock ticker) and [homeplate](https://github.com/awailly/homeplate)
(Inkplate 10), built the same way: PlatformIO, a `config_example.h` you copy
to `config.h`, deep sleep between refreshes.

## Display

```
GO   | 22h  00h  02h  04h
-----+-------------------
Cld% |  12   25   75   90
See" | 0.75 1.25 1.25 2.5+
Mag  | 0.4  0.5  0.6  1+
---------------------------
Moon 100% up        Sat 28
dark 22:00      27/08 18:05
```

- **Grid** — one column every `FORECAST_SLOT_HOURS` (default 2 h), starting
  at the first full hour of darkness (Sun below `DUSK_SUN_ALT`). Rows: cloud
  cover (%), seeing (arc seconds, upper bound of the 7Timer code) and
  transparency (magnitudes of extinction per air mass — lower is better).
  Cells at NO GO levels are red.
- **Verdict** — top-left corner: `GO` / `RISK` / `NO`, red when `NO`.
- **Sky line** — Moon illumination and whether it is up at `OBS_HOUR`, then
  the two highest planets (of Venus, Mars, Jupiter, Saturn) above
  `PLANET_MIN_ALT` with their altitude in degrees.
- **Footer** — exact time of darkness, uptime since last power loss (see
  Battery), and the last update time.

## Verdict

Computed over the displayed slots (default: dark → dark + 8 h):

- **NO** — precipitation forecast, thunderstorm risk (lifted index ≤ −4),
  or average cloud cover ≥ 50%
- **GO** — average cloud cover ≤ 25% and seeing better than ~2"
- **RISK** — everything in between

The Moon does not gate the verdict: it barely matters for planetary and lunar
work. Its illumination is displayed so deep-sky nights can be ruled out at a
glance. All thresholds live in `config.h`.

## Setup

1. Clone this repo and open the folder in VS Code, install the recommended
   [PlatformIO](https://platformio.org/) extension when prompted.
2. Copy `src/config_example.h` to `src/config.h` and set at least:
   - `WIFI_SSID` / `WIFI_PASSWORD`
   - `LATITUDE` / `LONGITUDE` (your observation site, decimal degrees)
   - `TIMEZONE_TZ` (defaults to Europe/Paris)
3. Connect the Inkplate 2 over USB and run **PlatformIO: Upload** (or
   `pio run -t upload` from the terminal).

## Data sources

- **Forecast** — the free, unauthenticated
  [7Timer ASTRO API](http://www.7timer.info/doc.php)
  (`https://www.7timer.info/bin/astro.php`), a 72 h forecast made for
  astronomers: cloud cover (1–9), astronomical seeing (1–8) and transparency
  (1–8), in 3-hour blocks. HTTPS certificate validation is skipped
  (`setInsecure()`): the data is public and read-only, and this avoids
  shipping/rotating root certificates on the device.
- **Ephemeris** — computed on the ESP32, no network needed, using Paul
  Schlyter's low-precision algorithms
  ([stjarnhimlen.se/comp/ppcomp.html](https://stjarnhimlen.se/comp/ppcomp.html)):
  Sun (dusk time), Moon (illumination, altitude, with the main perturbation
  terms and parallax) and planet altitudes. Accuracy is a fraction of a
  degree — plenty for "is Saturn up tonight".

## Behavior

- Refreshes every `TIME_TO_SLEEP_MIN` minutes (default 180), then deep
  sleeps. The Inkplate 2 has no RTC chip or wake button, so the timer is the
  only wake source.
- A valid clock is required (forecast window + ephemeris), so NTP failure on
  a fresh boot draws an error screen and retries after
  `TIME_TO_SLEEP_ERROR_MIN` minutes (default 15).
- If the forecast fetch fails, the Moon/planets row is still drawn (it is
  computed locally), the verdict shows `?`, and the retry uses the error
  interval.
- After midnight "tonight" still means the ongoing night, until 6 am.
- Optional: define `NIGHT_START_HOUR` / `NIGHT_END_HOUR` in `config.h` to
  skip refreshes at night and save battery — by then you are either outside
  or asleep.

## Battery

The Inkplate 2 cannot measure its battery voltage (unlike the bigger
Inkplates), so no battery gauge is possible. Instead the footer shows an
uptime counter (`up 12d`): time since the last power loss, kept in RTC
memory across deep sleeps. Once you know how long a charge lasts, it doubles
as a "recharge soon" indicator. With ~28 s awake per refresh every 3 h (the
red e-paper is slow), a 600 mAh cell should last a few months.

## Project layout

| File | Purpose |
| --- | --- |
| `src/main.cpp` | boot → WiFi → NTP → fetch → compute → render → deep sleep |
| `src/network.cpp` | WiFi connection and NTP sync |
| `src/forecast.cpp` | 7Timer fetch + JSON parsing + verdict |
| `src/ephem.cpp` | Sun/Moon/planet positions (Schlyter low-precision) |
| `src/render.cpp` | screen layout |
| `src/config_example.h` | template for `src/config.h` (gitignored) |
| `src/fonts/` | Roboto GFX fonts (Apache 2.0), from homeplate |

## License

MIT — see [LICENSE](LICENSE). The Roboto fonts in `src/fonts/` are licensed
under Apache 2.0.
