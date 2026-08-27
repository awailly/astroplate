#include <WiFi.h>
#include "astroplate.h"

#define WIFI_TIMEOUT_MS (15 * SECOND)
#define WIFI_MAX_RETRIES 3
#define NTP_TIMEOUT_MS (10 * SECOND)

bool wifiConnect()
{
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME); // only works with DHCP....

    for (int attempt = 1; attempt <= WIFI_MAX_RETRIES; attempt++)
    {
        Serial.printf("[WIFI] Connecting to %s (attempt %d/%d)...\n", WIFI_SSID, attempt, WIFI_MAX_RETRIES);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        unsigned long startAttemptTime = millis();
        while (WiFi.status() != WL_CONNECTED &&
               millis() - startAttemptTime < WIFI_TIMEOUT_MS)
        {
            delay(100);
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.println("[WIFI] Connected: " + WiFi.localIP().toString());
            return true;
        }

        Serial.printf("[WIFI] FAILED (attempt %d/%d)\n", attempt, WIFI_MAX_RETRIES);
        WiFi.disconnect();
        delay(1 * SECOND);
    }
    return false;
}

void wifiDisconnect()
{
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

// Sync the internal RTC over NTP. Only needed for the "updated at" footer and
// the optional night check, so failure is not fatal.
bool syncTime()
{
    configTzTime(TIMEZONE_TZ, NTP_SERVER);
    struct tm timeInfo;
    if (!getLocalTime(&timeInfo, NTP_TIMEOUT_MS))
    {
        Serial.println("[TIME] NTP sync failed");
        return false;
    }
    Serial.printf("[TIME] synced: %04d-%02d-%02d %02d:%02d\n",
                  timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday,
                  timeInfo.tm_hour, timeInfo.tm_min);
    return true;
}
