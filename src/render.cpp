#include "astroplate.h"
#include "fonts/Roboto_12.h"
#include "fonts/Roboto_16.h"
#include "fonts/Roboto_32.h"

// Layout for the 212x104 landscape screen: a verdict row, a Moon/planets
// row, and a small footer with dusk time and update time
#define ROW_HEIGHT 46
#define MARGIN 4
#define FOOTER_Y 94

static const char *VERDICT_TEXT[] = {"GO", "MAYBE", "NO GO"};

// upper bound of each 7Timer seeing code, in arc seconds
static const char *SEEING_TEXT[9] = {"?", "0.5\"", "0.75\"", "1\"", "1.25\"",
                                     "1.5\"", "2\"", "2.5\"", ">2.5\""};

static uint16_t textWidth(const char *text)
{
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    return w;
}

static void printRightAligned(const char *text, int16_t xRight, int16_t yBaseline)
{
    display.setCursor(xRight - textWidth(text), yBaseline);
    display.print(text);
}

// "Tonight" + cloud/seeing summary on the left, big verdict on the right,
// red when it is a NO GO (or unknown)
static void drawVerdictRow(const Forecast &f)
{
    display.setFont(&Roboto_12);
    display.setTextColor(INKPLATE2_BLACK);
    display.setCursor(MARGIN, 17);
    display.print("Tonight");

    char cond[40];
    if (!f.valid)
        strlcpy(cond, "no forecast", sizeof(cond));
    else if (f.precipitation)
        snprintf(cond, sizeof(cond), "%d%% cloud, rain", f.cloudsPct);
    else
        snprintf(cond, sizeof(cond), "%d%% cloud  %s", f.cloudsPct,
                 (f.seeingCode >= 1 && f.seeingCode <= 8) ? SEEING_TEXT[f.seeingCode] : "?");
    display.setCursor(MARGIN, 39);
    display.print(cond);

    const char *verdict = f.valid ? VERDICT_TEXT[f.verdict] : "?";
    bool bad = !f.valid || f.verdict == VERDICT_NOGO;
    display.setTextColor(bad ? INKPLATE2_RED : INKPLATE2_BLACK);

    // use the big font if the verdict fits next to the left column, else the smaller one
    int16_t leftColumn = MARGIN + max(textWidth("Tonight"), textWidth(cond));
    display.setFont(&Roboto_32);
    if (leftColumn + 8 + textWidth(verdict) + MARGIN > SCREEN_WIDTH)
    {
        display.setFont(&Roboto_16);
        printRightAligned(verdict, SCREEN_WIDTH - MARGIN, 30);
    }
    else
    {
        printRightAligned(verdict, SCREEN_WIDTH - MARGIN, 37);
    }
    display.setTextColor(INKPLATE2_BLACK);
}

// Moon illumination + whether it is up at OBS_HOUR, then the planets above
// PLANET_MIN_ALT with their altitude in degrees, highest first
static void drawSkyRow(const Ephemeris &eph)
{
    display.setFont(&Roboto_12);
    display.setTextColor(INKPLATE2_BLACK);

    char moon[32];
    snprintf(moon, sizeof(moon), "Moon %d%%  %s", eph.moonIllumPct,
             eph.moonAlt > 0 ? "up" : "down");
    display.setCursor(MARGIN, ROW_HEIGHT + 19);
    display.print(moon);

    PlanetInfo up[4];
    int n = 0;
    for (int i = 0; i < 4; i++)
        if (eph.planets[i].alt >= PLANET_MIN_ALT)
        {
            // insertion sort by altitude, highest first
            int j = n++;
            while (j > 0 && up[j - 1].alt < eph.planets[i].alt)
            {
                up[j] = up[j - 1];
                j--;
            }
            up[j] = eph.planets[i];
        }

    char line[48] = "";
    for (int i = 0; i < n && i < 3; i++)
    {
        char one[16];
        snprintf(one, sizeof(one), "%s%s %.0f", i ? "  " : "", up[i].name, up[i].alt);
        strlcat(line, one, sizeof(line));
    }
    if (n == 0)
        strlcpy(line, "no planet up", sizeof(line));
    display.setCursor(MARGIN, ROW_HEIGHT + 41);
    display.print(line);
}

static void drawFooter(time_t dusk)
{
    display.setFont(NULL); // built-in 5x7 font
    display.setTextSize(1);
    display.setTextColor(INKPLATE2_BLACK);

    if (dusk)
    {
        struct tm duskInfo;
        localtime_r(&dusk, &duskInfo);
        char text[24];
        snprintf(text, sizeof(text), "dark %02d:%02d", duskInfo.tm_hour, duskInfo.tm_min);
        display.setCursor(MARGIN, FOOTER_Y);
        display.print(text);
    }

    struct tm timeInfo;
    if (getLocalTime(&timeInfo, 100))
    {
        char updated[32];
        snprintf(updated, sizeof(updated), "%02d/%02d %02d:%02d",
                 timeInfo.tm_mday, timeInfo.tm_mon + 1, timeInfo.tm_hour, timeInfo.tm_min);
        printRightAligned(updated, SCREEN_WIDTH - MARGIN, FOOTER_Y);
    }
}

void renderStatus(const Forecast &f, const Ephemeris &eph)
{
    Serial.println("[RENDER] drawing status");
    display.clearDisplay();

    drawVerdictRow(f);
    display.drawFastHLine(0, ROW_HEIGHT, SCREEN_WIDTH, INKPLATE2_BLACK);
    drawSkyRow(eph);
    drawFooter(eph.dusk);

    display.display();
    Serial.println("[RENDER] done");
}

void renderError(const char *format, ...)
{
    char message[128];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    Serial.printf("[RENDER] error screen: %s\n", message);
    display.clearDisplay();

    display.setFont(&Roboto_12);
    display.setTextColor(INKPLATE2_RED);
    display.setCursor(MARGIN, 20);
    display.print("Error");

    display.setTextColor(INKPLATE2_BLACK);
    int16_t y = 44;
    // print line by line, setCursor + print does not handle '\n' with GFX fonts
    char *saveptr;
    for (char *line = strtok_r(message, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr))
    {
        display.setCursor(MARGIN, y);
        display.print(line);
        y += 22;
    }

    display.display();
}
