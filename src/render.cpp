#include "astroplate.h"
#include "fonts/Roboto_12.h"
#include "fonts/Roboto_16.h"

// Layout for the 212x104 landscape screen: a forecast grid (verdict corner,
// one column per FORECAST_SLOT_HOURS, rows clouds/seeing/transparency), a
// Moon + planets line, and a tiny footer with dusk and update time.
//
//   GO   | 22h  00h  02h  04h
//   -----+-------------------
//   Cld% |  12   25   75   90
//   See" | 0.75 1.25 1.25 2.5+
//   Mag  | 0.4  0.5  0.6  1+
//   ---------------------------
//   Moon 100% up        Sat 28
//   dark 22:00      27/08 18:05
#define MARGIN 2
#define GRID_LEFT 44
#define HEADER_BASELINE 15
#define ROW_BASELINE(r) (35 + 16 * (r))
#define GRID_TOP_LINE 19
#define GRID_BOTTOM_LINE 71
#define SKY_BASELINE 87
#define FOOTER_Y 96

static const char *VERDICT_TEXT[] = {"GO", "RISK", "NO"};

// upper bound of each 7Timer seeing code, arc seconds (unit in the row label)
static const char *SEEING_TEXT[9] = {"-", "0.5", "0.75", "1", "1.25", "1.5", "2", "2.5", "2.5+"};

// upper bound of each 7Timer transparency code, magnitudes of extinction per
// air mass (lower is better, >1 means murky)
static const char *TRANSP_TEXT[9] = {"-", "0.3", "0.4", "0.5", "0.6", "0.7", "0.85", "1.0", "1+"};

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

static int16_t colCenter(int i)
{
    return GRID_LEFT + (SCREEN_WIDTH - GRID_LEFT) * (2 * i + 1) / (2 * FORECAST_SLOTS);
}

static void printCentered(const char *text, int16_t xCenter, int16_t yBaseline)
{
    display.setCursor(xCenter - textWidth(text) / 2, yBaseline);
    display.print(text);
}

// verdict in the top-left corner, red when NO (or unknown), then one column
// of hour/clouds/seeing/transparency per slot, bad cells in red
static void drawGrid(const Forecast &f)
{
    const char *verdict = f.valid ? VERDICT_TEXT[f.verdict] : "?";
    bool bad = !f.valid || f.verdict == VERDICT_NOGO;
    display.setTextColor(bad ? INKPLATE2_RED : INKPLATE2_BLACK);
    display.setFont(&Roboto_16);
    if (textWidth(verdict) > GRID_LEFT - 2 * MARGIN)
        display.setFont(&Roboto_12);
    display.setCursor(MARGIN, HEADER_BASELINE);
    display.print(verdict);
    display.setTextColor(INKPLATE2_BLACK);

    display.drawFastHLine(0, GRID_TOP_LINE, SCREEN_WIDTH, INKPLATE2_BLACK);
    display.drawFastVLine(GRID_LEFT - 4, 0, GRID_BOTTOM_LINE, INKPLATE2_BLACK);
    display.drawFastHLine(0, GRID_BOTTOM_LINE, SCREEN_WIDTH, INKPLATE2_BLACK);

    display.setFont(&Roboto_12);
    static const char *ROW_LABEL[3] = {"Cld%", "See\"", "Mag"};
    for (int r = 0; r < 3; r++)
    {
        display.setCursor(MARGIN, ROW_BASELINE(r));
        display.print(ROW_LABEL[r]);
    }

    if (!f.valid)
    {
        printCentered("no forecast", (GRID_LEFT + SCREEN_WIDTH) / 2, ROW_BASELINE(1));
        return;
    }

    for (int i = 0; i < FORECAST_SLOTS; i++)
    {
        const ForecastSlot &s = f.slots[i];
        int16_t x = colCenter(i);

        char hour[8];
        snprintf(hour, sizeof(hour), "%02dh", s.hour);
        printCentered(hour, x, HEADER_BASELINE);

        if (!s.valid)
        {
            for (int r = 0; r < 3; r++)
                printCentered("-", x, ROW_BASELINE(r));
            continue;
        }

        char clouds[8];
        snprintf(clouds, sizeof(clouds), "%d", s.cloudsPct);
        display.setTextColor(s.cloudsPct >= CLOUDS_NOGO_MIN_PCT ? INKPLATE2_RED : INKPLATE2_BLACK);
        printCentered(clouds, x, ROW_BASELINE(0));

        bool seeingOk = s.seeingCode >= 1 && s.seeingCode <= 8;
        display.setTextColor(seeingOk && s.seeingCode >= SEEING_MAYBE_CODE ? INKPLATE2_RED
                                                                           : INKPLATE2_BLACK);
        printCentered(SEEING_TEXT[seeingOk ? s.seeingCode : 0], x, ROW_BASELINE(1));

        display.setTextColor(INKPLATE2_BLACK);
        bool transpOk = s.transpCode >= 1 && s.transpCode <= 8;
        printCentered(TRANSP_TEXT[transpOk ? s.transpCode : 0], x, ROW_BASELINE(2));
    }
    display.setTextColor(INKPLATE2_BLACK);
}

// Moon illumination + whether it is up at OBS_HOUR on the left, the two
// highest planets above PLANET_MIN_ALT with their altitude on the right
static void drawSkyRow(const Ephemeris &eph)
{
    display.setFont(&Roboto_12);
    display.setTextColor(INKPLATE2_BLACK);

    char moon[24];
    snprintf(moon, sizeof(moon), "Moon %d%% %s", eph.moonIllumPct,
             eph.moonAlt > 0 ? "up" : "down");
    display.setCursor(MARGIN, SKY_BASELINE);
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

    char line[32] = "";
    for (int i = 0; i < n && i < 2; i++)
    {
        char one[16];
        snprintf(one, sizeof(one), "%s%s %.0f", i ? "  " : "", up[i].name, up[i].alt);
        strlcat(line, one, sizeof(line));
    }
    if (n == 0)
        strlcpy(line, "no planet", sizeof(line));
    printRightAligned(line, SCREEN_WIDTH - MARGIN, SKY_BASELINE);
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

    drawGrid(f);
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
