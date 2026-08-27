#pragma once

#include <Arduino.h>
#include <time.h>
#include "Inkplate.h"
#include "config.h"

#ifndef CONFIG_H
#error "Missing config! Copy src/config_example.h to src/config.h and edit it."
#endif

#define VERSION "0.1.0"
#define SECOND 1000 // ms

// Inkplate 2 in default (landscape) rotation: 212x104
#define SCREEN_WIDTH E_INK_HEIGHT
#define SCREEN_HEIGHT E_INK_WIDTH

extern Inkplate display;

enum Verdict
{
    VERDICT_GO = 0,
    VERDICT_MAYBE = 1,
    VERDICT_NOGO = 2,
};

struct Forecast
{
    bool valid;
    int cloudsPct;        // average cloud cover over tonight's window, percent
    int seeingCode;       // worst 7Timer seeing code of the window (1-8, lower is better)
    int transparencyCode; // worst 7Timer transparency code (1-8, lower is better)
    bool precipitation;   // any rain/snow forecast during the window
    bool stormRisk;       // lifted index at or below LIFTED_INDEX_NOGO in the window
    Verdict verdict;
};

struct PlanetInfo
{
    const char *name;
    float alt; // altitude at OBS_HOUR, degrees
};

struct Ephemeris
{
    int moonIllumPct;      // illuminated fraction of the Moon, percent
    float moonAlt;         // Moon altitude at OBS_HOUR, degrees
    PlanetInfo planets[4]; // Venus, Mars, Jupiter, Saturn at OBS_HOUR
    time_t dusk;           // nautical dusk tonight, 0 if not found
};

// network.cpp
bool wifiConnect();
void wifiDisconnect();
bool syncTime();

// forecast.cpp
bool fetchForecast(Forecast &f);
bool fetchForecastRetry(Forecast &f, uint32_t trys);

// ephem.cpp
void computeEphemeris(time_t obsTime, Ephemeris &eph);
time_t findDusk(time_t from);

// render.cpp
void renderStatus(const Forecast &f, const Ephemeris &eph);
void renderError(const char *format, ...);
