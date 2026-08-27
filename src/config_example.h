#pragma once
#ifndef CONFIG_H

// WiFi SSID
#define WIFI_SSID "WiFi Network Name"
// WiFi password
#define WIFI_PASSWORD "WiFi Password"

// hostname
// NOTE: if using multiple astroplate devices, you MUST make the hostname unique
#define HOSTNAME "astroplate"

// Observation site, decimal degrees (north and east positive)
#define LATITUDE 48.9202
#define LONGITUDE 1.9693

// Forecast grid: FORECAST_SLOTS columns, one every FORECAST_SLOT_HOURS hours,
// starting at the first full hour of darkness (Sun below DUSK_SUN_ALT).
// The verdict is computed over these slots.
#define FORECAST_SLOTS 4
#define FORECAST_SLOT_HOURS 2

// Local hour the Moon and planet altitudes are computed for
#define OBS_HOUR 23

// Verdict thresholds on the average cloud cover (percent):
// GO at or below CLOUDS_GO_MAX_PCT, NO GO at or above CLOUDS_NOGO_MIN_PCT
// (or any precipitation), MAYBE in between
#define CLOUDS_GO_MAX_PCT 25
#define CLOUDS_NOGO_MIN_PCT 50

// Downgrade GO to MAYBE when the 7Timer seeing code reaches this value
// (7 = seeing worse than 2", hopeless for planetary work)
#define SEEING_MAYBE_CODE 7

// NO GO when the lifted index (atmospheric stability, degrees C) drops to
// this value or below: -4 or less means real thunderstorm risk, no night to
// leave a telescope out. 7Timer codes: -10, -6, -4, -1, 2, 6, 10, 15
#define LIFTED_INDEX_NOGO -4

// Only planets above this altitude (degrees) at OBS_HOUR are shown
#define PLANET_MIN_ALT 10

// Sun altitude defining "dark", degrees. -12 (nautical dusk) is dark enough
// for planetary work; use -18 (astronomical dusk) for deep sky
#define DUSK_SUN_ALT -12.0

// How long to sleep between refreshes
#define TIME_TO_SLEEP_MIN 180

// How long to sleep before retrying after an error (WiFi or data fetch failed)
#define TIME_TO_SLEEP_ERROR_MIN 15

// NTP server to sync the clock (required: the forecast window and the
// ephemeris both depend on it)
#define NTP_SERVER "pool.ntp.org"

// Timezone in POSIX TZ format
// Europe/Paris: "CET-1CEST,M3.5.0,M10.5.0/3"
// UTC: "UTC0"
// see https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
#define TIMEZONE_TZ "CET-1CEST,M3.5.0,M10.5.0/3"

// Optional: skip refreshes at night to save battery.
// When enabled, a wake up at or after NIGHT_START_HOUR (or before
// NIGHT_END_HOUR) goes back to sleep until NIGHT_END_HOUR without
// connecting to WiFi.
// #define NIGHT_START_HOUR 0
// #define NIGHT_END_HOUR 7

// keep this to signal the program has a valid config file
#define CONFIG_H
#endif
