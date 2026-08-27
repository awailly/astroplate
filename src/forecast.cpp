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

// Fill the forecast grid: one slot every FORECAST_SLOT_HOURS starting at
// nightStart, each taking the nearest 3h block (within 1.5h). The verdict
// is computed over the filled slots.
bool fetchForecast(Forecast &f, time_t nightStart)
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

    JsonArray series = doc["dataseries"].as<JsonArray>();
    int n = 0, cloudSum = 0, worstSeeing = 0;
    bool precip = false, storm = false;
    for (int i = 0; i < FORECAST_SLOTS; i++)
    {
        ForecastSlot &s = f.slots[i];
        time_t target = nightStart + (time_t)i * FORECAST_SLOT_HOURS * 3600;
        struct tm lt;
        localtime_r(&target, &lt);
        s.valid = false;
        s.hour = lt.tm_hour;

        JsonObject best;
        long bestDiff = 5400 + 1; // half a 3h block, else the slot stays empty
        for (JsonObject p : series)
        {
            long diff = labs((long)(initEpoch + (long)(p["timepoint"] | 0) * 3600 - target));
            if (diff < bestDiff)
            {
                bestDiff = diff;
                best = p;
            }
        }
        if (best.isNull())
            continue;

        int cc = best["cloudcover"] | 0;
        if (cc < 1 || cc > 9)
            continue;
        s.cloudsPct = CLOUD_PCT[cc];
        s.seeingCode = best["seeing"] | 0;
        s.transpCode = best["transparency"] | 0;
        s.valid = true;

        cloudSum += s.cloudsPct;
        n++;
        worstSeeing = max(worstSeeing, s.seeingCode);
        // lifted index: atmospheric stability, strongly negative -> thunderstorms
        if ((int)(best["lifted_index"] | 0) <= LIFTED_INDEX_NOGO)
            storm = true;
        const char *prec = best["prec_type"] | "none";
        if (strcmp(prec, "none") != 0)
            precip = true;
    }

    if (n == 0)
    {
        Serial.println("[FORECAST] no data blocks near tonight's slots");
        return false;
    }

    f.precipitation = precip;
    f.stormRisk = storm;

    int cloudsAvg = cloudSum / n;
    if (precip || storm || cloudsAvg >= CLOUDS_NOGO_MIN_PCT)
        f.verdict = VERDICT_NOGO;
    else if (cloudsAvg <= CLOUDS_GO_MAX_PCT && worstSeeing < SEEING_MAYBE_CODE)
        f.verdict = VERDICT_GO;
    else
        f.verdict = VERDICT_MAYBE;

    f.valid = true;
    Serial.printf("[FORECAST] %d slot(s): clouds avg %d%%, worst seeing %d/8, precip %d, storm %d -> verdict %d\n",
                  n, cloudsAvg, worstSeeing, precip, storm, f.verdict);
    return true;
}

bool fetchForecastRetry(Forecast &f, time_t nightStart, uint32_t trys)
{
    for (uint32_t i = 0; i < trys; i++)
    {
        if (fetchForecast(f, nightStart))
            return true;
        // wait before trying again
        delay(1 * SECOND);
    }
    return false;
}
