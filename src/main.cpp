// Next lines are a precaution, you can ignore those, and the example would also work without them
#ifndef ARDUINO_INKPLATE2
#error "Wrong board selection for this project, please select Soldered Inkplate 2 in the boards menu."
#endif

#include <WiFi.h>
#include "astroplate.h"

Inkplate display;

// Store int in rtc data, to remain persistent during deep sleep, reset on power up.
RTC_DATA_ATTR uint bootCount = 0;

// The Inkplate 2 cannot measure its battery, so the footer shows uptime
// since the last power loss instead: a fresh battery starts at "up 0h".
RTC_DATA_ATTR time_t powerOnEpoch = 0;

// Everything runs in setup(): the device wakes from deep sleep, refreshes the
// display and goes back to sleep, so loop() is never reached.
void gotoSleep(uint32_t minutes)
{
    Serial.printf("[SLEEP] going to sleep for %u minute(s)\n", minutes);
    Serial.flush();
    wifiDisconnect();
    esp_sleep_enable_timer_wakeup((uint64_t)minutes * 60ULL * 1000000ULL);
    esp_deep_sleep_start();
}

#if defined(NIGHT_START_HOUR) && defined(NIGHT_END_HOUR)
// If we woke up during the night window, go back to sleep until morning.
// Relies on the RTC keeping (approximate) time across deep sleep, which it
// does as long as the device stays powered.
void nightCheck()
{
    struct tm timeInfo;
    if (!getLocalTime(&timeInfo, 100)) // RTC time, no network needed
        return;                        // clock not set yet (fresh power on), skip
    int hour = timeInfo.tm_hour;
    bool night = (NIGHT_START_HOUR > NIGHT_END_HOUR)
                     ? (hour >= NIGHT_START_HOUR || hour < NIGHT_END_HOUR)
                     : (hour >= NIGHT_START_HOUR && hour < NIGHT_END_HOUR);
    if (!night)
        return;
    int minutesToMorning = ((NIGHT_END_HOUR - hour + 24) % 24) * 60 - timeInfo.tm_min;
    if (minutesToMorning <= 0)
        minutesToMorning = TIME_TO_SLEEP_MIN;
    Serial.printf("[SLEEP] night time (%02d:%02d), sleeping until %d:00\n",
                  timeInfo.tm_hour, timeInfo.tm_min, NIGHT_END_HOUR);
    gotoSleep(minutesToMorning);
}
#endif

// Local time of `hour` o'clock during the night being forecast: today if we
// are in the afternoon/evening, yesterday if we are already past midnight
// (so "tonight" keeps meaning the ongoing night until morning).
static time_t tonightAt(int hour)
{
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    if (lt.tm_hour < 6)
    {
        time_t yesterday = now - 24 * 3600;
        localtime_r(&yesterday, &lt);
    }
    lt.tm_hour = hour;
    lt.tm_min = 0;
    lt.tm_sec = 0;
    lt.tm_isdst = -1;
    return mktime(&lt);
}

void setup()
{
    Serial.begin(115200);
    Serial.printf("\n\n[SETUP] starting, version(%s) boot: %u\n", VERSION, bootCount);
    ++bootCount;

#if defined(NIGHT_START_HOUR) && defined(NIGHT_END_HOUR)
    nightCheck(); // may go back to sleep without touching WiFi or the display
#endif

    // Init Inkplate library (you should call this function ONLY ONCE).
    // begin() allocates the framebuffer in PSRAM; on failure the buffer is
    // NULL and any drawing call would crash, so sleep and retry instead.
    if (!display.begin())
    {
        Serial.println("[SETUP] display.begin() failed (framebuffer allocation or panel init)");
        gotoSleep(TIME_TO_SLEEP_ERROR_MIN);
    }
    display.setTextWrap(false);
    display.setTextColor(INKPLATE2_BLACK, INKPLATE2_WHITE);

    if (!wifiConnect())
    {
        Serial.println("[SETUP] WiFi connection failed");
        renderError("WiFi failed\n%s", WIFI_SSID);
        gotoSleep(TIME_TO_SLEEP_ERROR_MIN);
    }

    syncTime();

    // Unlike stockplate, a correct clock is required here: the forecast
    // window and the ephemeris both depend on it. Refuse to display
    // nonsense if the RTC was never set.
    struct tm timeInfo;
    if (!getLocalTime(&timeInfo, 100) || timeInfo.tm_year + 1900 < 2020)
    {
        Serial.println("[SETUP] no valid clock, cannot compute tonight");
        renderError("No clock\nNTP sync failed");
        gotoSleep(TIME_TO_SLEEP_ERROR_MIN);
    }

    // anchor the uptime counter on the first boot with a valid clock
    if (powerOnEpoch == 0)
        powerOnEpoch = time(nullptr);

    // Ephemeris first, it is local-only and the forecast grid starts at
    // dusk: Moon and planets at OBS_HOUR tonight, dusk searched from
    // mid-afternoon on.
    Ephemeris eph = {};
    computeEphemeris(tonightAt(OBS_HOUR), eph);
    eph.dusk = findDusk(tonightAt(15));

    // grid columns start at the first full hour of darkness
    time_t nightStart = eph.dusk ? ((eph.dusk + 3599) / 3600) * 3600 : tonightAt(22);

    Forecast forecast = {};
    fetchForecastRetry(forecast, nightStart, 3);

    // Even with no forecast the Moon/planets half is worth showing; the
    // verdict renders as "?" and we retry on the error interval.
    renderStatus(forecast, eph);
    gotoSleep(forecast.valid ? TIME_TO_SLEEP_MIN : TIME_TO_SLEEP_ERROR_MIN);
}

void loop()
{
    // Never reached: setup() always ends in deep sleep.
}
