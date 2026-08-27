#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "astroplate.h"

#define HTTP_TIMEOUT_MS (20 * SECOND)

// 7Timer ASTRO product: free, no API key, made for astronomers. Returns 72h
// of 3-hourly blocks with cloud cover, astronomical seeing and transparency.
// http://www.7timer.info/doc.php
#define SEVENTIMER_URL "https://www.7timer.info/bin/astro.php"

#define USER_AGENT "Mozilla/5.0 (X11; Linux x86_64) astroplate/" VERSION

// midpoint percentage of each 7Timer cloud cover code (1..9)
static const int CLOUD_PCT[10] = {0, 3, 12, 25, 37, 50, 62, 75, 87, 97};

// "init" is the model run in UTC, e.g. "2026082712" -> epoch of that hour
static time_t parseInit(const char *init)
{
    int y, mo, dd, hh;
    if (!init || sscanf(init, "%4d%2d%2d%2d", &y, &mo, &dd, &hh) != 4)
        return 0;
    // days-from-civil via Julian day number, avoids touching the TZ database
    int a = (14 - mo) / 12;
    long yy = y + 4800L - a;
    int mm = mo + 12 * a - 3;
    long jdn = dd + (153L * mm + 2) / 5 + 365L * yy + yy / 4 - yy / 100 + yy / 400 - 32045L;
    return (time_t)(jdn - 2440588L) * 86400 + (time_t)hh * 3600;
}

// Tonight's observation window in local time: WINDOW_START_HOUR today to
// WINDOW_END_HOUR tomorrow. Past midnight the ongoing night still counts.
static void tonightWindow(time_t &start, time_t &end)
{
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    if (lt.tm_hour < 6)
    {
        time_t yesterday = now - 24 * 3600;
        localtime_r(&yesterday, &lt);
    }
    lt.tm_hour = WINDOW_START_HOUR;
    lt.tm_min = 0;
    lt.tm_sec = 0;
    lt.tm_isdst = -1;
    start = mktime(&lt);
    end = start + ((24 + WINDOW_END_HOUR - WINDOW_START_HOUR) % 24) * 3600L;
}

bool fetchForecast(Forecast &f)
{
    f.valid = false;

    char url[160];
    snprintf(url, sizeof(url), "%s?lon=%.4f&lat=%.4f&ac=0&unit=metric&output=json&tzshift=0",
             SEVENTIMER_URL, (double)LONGITUDE, (double)LATITUDE);
    Serial.printf("[FORECAST] fetching %s\n", url);

    WiFiClientSecure client;
    client.setInsecure(); // skip certificate validation, weather data is not sensitive

    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    if (!http.begin(client, url))
    {
        Serial.println("[FORECAST] http.begin() failed");
        return false;
    }
    http.addHeader("User-Agent", USER_AGENT);

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK)
    {
        Serial.printf("[FORECAST] Non-200 response: %d\n", httpCode);
        http.end();
        return false;
    }

    // getString() handles chunked transfer encoding; the astro product is
    // ~15 KB for 72h so it fits in RAM without PSRAM.
    String payload = http.getString();
    http.end();

    // Only keep the fields we care about to save RAM while parsing
    JsonDocument filter;
    filter["init"] = true;
    JsonObject pointFilter = filter["dataseries"][0].to<JsonObject>();
    pointFilter["timepoint"] = true;
    pointFilter["cloudcover"] = true;
    pointFilter["seeing"] = true;
    pointFilter["transparency"] = true;
    pointFilter["lifted_index"] = true;
    pointFilter["prec_type"] = true;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
    if (error)
    {
        Serial.printf("[FORECAST] JSON parse failed: %s\n", error.c_str());
        return false;
    }

    time_t initEpoch = parseInit(doc["init"] | (const char *)nullptr);
    if (initEpoch == 0)
    {
        Serial.println("[FORECAST] missing/invalid init timestamp");
        return false;
    }

    time_t start, end;
    tonightWindow(start, end);

    // Average the clouds and keep the worst seeing/transparency over the
    // blocks that fall in tonight's window (blocks are 3h wide, so accept
    // one hour of slack on each side).
    int n = 0, cloudSum = 0, worstSeeing = 0, worstTransp = 0;
    bool precip = false, storm = false;
    for (JsonObject p : doc["dataseries"].as<JsonArray>())
    {
        time_t t = initEpoch + (long)(p["timepoint"] | 0) * 3600;
        if (t < start - 3600 || t > end + 3600)
            continue;
        int cc = p["cloudcover"] | 0;
        if (cc < 1 || cc > 9)
            continue;
        cloudSum += CLOUD_PCT[cc];
        n++;
        worstSeeing = max(worstSeeing, (int)(p["seeing"] | 0));
        worstTransp = max(worstTransp, (int)(p["transparency"] | 0));
        // lifted index: atmospheric stability, strongly negative -> thunderstorms
        if ((int)(p["lifted_index"] | 0) <= LIFTED_INDEX_NOGO)
            storm = true;
        const char *prec = p["prec_type"] | "none";
        if (strcmp(prec, "none") != 0)
            precip = true;
    }

    if (n == 0)
    {
        Serial.println("[FORECAST] no data points in tonight's window");
        return false;
    }

    f.cloudsPct = cloudSum / n;
    f.seeingCode = worstSeeing;
    f.transparencyCode = worstTransp;
    f.precipitation = precip;
    f.stormRisk = storm;

    if (precip || storm || f.cloudsPct >= CLOUDS_NOGO_MIN_PCT)
        f.verdict = VERDICT_NOGO;
    else if (f.cloudsPct <= CLOUDS_GO_MAX_PCT && f.seeingCode < SEEING_MAYBE_CODE)
        f.verdict = VERDICT_GO;
    else
        f.verdict = VERDICT_MAYBE;

    f.valid = true;
    Serial.printf("[FORECAST] %d block(s): clouds %d%%, seeing %d/8, transparency %d/8, precip %d, storm %d -> verdict %d\n",
                  n, f.cloudsPct, f.seeingCode, f.transparencyCode, precip, storm, f.verdict);
    return true;
}

bool fetchForecastRetry(Forecast &f, uint32_t trys)
{
    for (uint32_t i = 0; i < trys; i++)
    {
        if (fetchForecast(f))
            return true;
        // wait before trying again
        delay(1 * SECOND);
    }
    return false;
}
