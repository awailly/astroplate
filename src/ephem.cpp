#include <math.h>
#include "astroplate.h"

// Low-precision ephemerides after Paul Schlyter's "How to compute planetary
// positions" (https://stjarnhimlen.se/comp/ppcomp.html). With the Moon
// perturbation terms below this is good to a few arc minutes for the Moon
// and well under a degree for the planets — plenty to answer "is it up
// tonight and how high".

#define DEG (M_PI / 180.0)
static double sind(double x) { return sin(x * DEG); }
static double cosd(double x) { return cos(x * DEG); }
static double atan2d(double y, double x) { return atan2(y, x) / DEG; }
static double asind(double x) { return asin(x) / DEG; }

// normalize an angle to [0, 360)
static double rev(double x)
{
    x = fmod(x, 360.0);
    return x < 0 ? x + 360.0 : x;
}

// everything derived from the observation instant, computed once
struct SkyContext
{
    double d;          // Schlyter day number (days since 2000-01-00.0)
    double ecl;        // obliquity of the ecliptic, degrees
    double lstDeg;     // local sidereal time, degrees
    double sunLon;     // Sun's true ecliptic longitude, degrees
    double sunX, sunY; // Sun's ecliptic rectangular position, AU (z = 0)
};

static void initContext(time_t t, SkyContext &c)
{
    struct tm utc;
    gmtime_r(&t, &utc);
    double ut = utc.tm_hour + utc.tm_min / 60.0 + utc.tm_sec / 3600.0;
    int y = utc.tm_year + 1900, m = utc.tm_mon + 1, D = utc.tm_mday;
    // integer divisions intended, this is the textbook formula
    c.d = 367L * y - 7L * (y + (m + 9) / 12) / 4 + 275L * m / 9 + D - 730530L + ut / 24.0;
    c.ecl = 23.4393 - 3.563e-7 * c.d;

    // the Sun: needed on its own (dusk) and to make planets geocentric
    double w = 282.9404 + 4.70935e-5 * c.d;
    double e = 0.016709 - 1.151e-9 * c.d;
    double M = rev(356.0470 + 0.9856002585 * c.d);
    double E = M + e * (180.0 / M_PI) * sind(M) * (1.0 + e * cosd(M));
    double xv = cosd(E) - e;
    double yv = sqrt(1.0 - e * e) * sind(E);
    double v = atan2d(yv, xv);
    double r = sqrt(xv * xv + yv * yv);
    c.sunLon = rev(v + w);
    c.sunX = r * cosd(c.sunLon);
    c.sunY = r * sind(c.sunLon);

    double gmst0 = rev(M + w + 180.0);
    c.lstDeg = rev(gmst0 + ut * 15.0 + LONGITUDE);
}

// geocentric ecliptic rectangular coordinates -> altitude above the horizon
static double eclipticToAltitude(const SkyContext &c, double xg, double yg, double zg)
{
    double xe = xg;
    double ye = yg * cosd(c.ecl) - zg * sind(c.ecl);
    double ze = yg * sind(c.ecl) + zg * cosd(c.ecl);
    double ra = rev(atan2d(ye, xe));
    double dec = atan2d(ze, sqrt(xe * xe + ye * ye));
    double ha = c.lstDeg - ra;
    return asind(sind(LATITUDE) * sind(dec) + cosd(LATITUDE) * cosd(dec) * cosd(ha));
}

struct Elements
{
    double N, i, w, a, e, M; // longitude of node, inclination, arg. of perihelion,
                             // semi-major axis, eccentricity, mean anomaly
};

// solve Kepler's equation and rotate into ecliptic coordinates (units of a)
static void orbitalToEcliptic(const Elements &el, double &x, double &y, double &z)
{
    double E = el.M + el.e * (180.0 / M_PI) * sind(el.M) * (1.0 + el.e * cosd(el.M));
    for (int k = 0; k < 10; k++)
    {
        double E1 = E - (E - (180.0 / M_PI) * el.e * sind(E) - el.M) / (1.0 - el.e * cosd(E));
        double delta = fabs(E1 - E);
        E = E1;
        if (delta < 1e-4)
            break;
    }
    double xv = el.a * (cosd(E) - el.e);
    double yv = el.a * sqrt(1.0 - el.e * el.e) * sind(E);
    double v = atan2d(yv, xv);
    double r = sqrt(xv * xv + yv * yv);
    x = r * (cosd(el.N) * cosd(v + el.w) - sind(el.N) * sind(v + el.w) * cosd(el.i));
    y = r * (sind(el.N) * cosd(v + el.w) + cosd(el.N) * sind(v + el.w) * cosd(el.i));
    z = r * sind(v + el.w) * sind(el.i);
}

static double sunAltitude(const SkyContext &c)
{
    return eclipticToAltitude(c, c.sunX, c.sunY, 0.0);
}

// heliocentric elements -> geocentric altitude
static double planetAltitude(const SkyContext &c, const Elements &el)
{
    double xh, yh, zh;
    orbitalToEcliptic(el, xh, yh, zh);
    return eclipticToAltitude(c, xh + c.sunX, yh + c.sunY, zh);
}

static double moonState(const SkyContext &c, int &illumPct)
{
    Elements el;
    el.N = rev(125.1228 - 0.0529538083 * c.d);
    el.i = 5.1454;
    el.w = rev(318.0634 + 0.1643573223 * c.d);
    el.a = 60.2666; // Earth radii
    el.e = 0.054900;
    el.M = rev(115.3654 + 13.0649929509 * c.d);

    double x, y, z;
    orbitalToEcliptic(el, x, y, z); // already geocentric for the Moon
    double lon = rev(atan2d(y, x));
    double lat = atan2d(z, sqrt(x * x + y * y));
    double r = sqrt(x * x + y * y + z * z);

    // main perturbation terms; without them the Moon can be off by 1.5 deg
    double Ms = rev(356.0470 + 0.9856002585 * c.d);   // Sun's mean anomaly
    double Ls = rev(Ms + 282.9404 + 4.70935e-5 * c.d); // Sun's mean longitude
    double Lm = rev(el.M + el.w + el.N);               // Moon's mean longitude
    double D = rev(Lm - Ls);                           // mean elongation
    double F = rev(Lm - el.N);                         // argument of latitude
    lon += -1.274 * sind(el.M - 2 * D) + 0.658 * sind(2 * D) - 0.186 * sind(Ms)
           - 0.059 * sind(2 * el.M - 2 * D) - 0.057 * sind(el.M - 2 * D + Ms)
           + 0.053 * sind(el.M + 2 * D) + 0.046 * sind(2 * D - Ms)
           + 0.041 * sind(el.M - Ms) - 0.035 * sind(D) - 0.031 * sind(el.M + Ms);
    lat += -0.173 * sind(F - 2 * D) - 0.055 * sind(el.M - F - 2 * D)
           - 0.046 * sind(el.M + F - 2 * D) + 0.033 * sind(F + 2 * D)
           + 0.017 * sind(2 * el.M + F);
    r += -0.58 * cosd(el.M - 2 * D) - 0.46 * cosd(2 * D);

    // illuminated fraction from the elongation to the Sun
    double elong = acos(cosd(c.sunLon - lon) * cosd(lat)) / DEG;
    illumPct = (int)lround((1.0 - cosd(elong)) / 2.0 * 100.0);

    double alt = eclipticToAltitude(c, r * cosd(lon) * cosd(lat),
                                    r * sind(lon) * cosd(lat), r * sind(lat));
    // the Moon is close enough that parallax lowers it by up to ~1 deg
    alt -= asind(1.0 / r) * cosd(alt);
    return alt;
}

void computeEphemeris(time_t obsTime, Ephemeris &eph)
{
    SkyContext c;
    initContext(obsTime, c);

    eph.moonAlt = moonState(c, eph.moonIllumPct);

    const double d = c.d;
    Elements venus = {rev(76.6799 + 2.46590e-5 * d), 3.3946 + 2.75e-8 * d,
                      rev(54.8910 + 1.38374e-5 * d), 0.723330,
                      0.006773 - 1.302e-9 * d, rev(48.0052 + 1.6021302244 * d)};
    Elements mars = {rev(49.5574 + 2.11081e-5 * d), 1.8497 - 1.78e-8 * d,
                     rev(286.5016 + 2.92961e-5 * d), 1.523688,
                     0.093405 + 2.516e-9 * d, rev(18.6021 + 0.5240207766 * d)};
    Elements jupiter = {rev(100.4542 + 2.76854e-5 * d), 1.3030 - 1.557e-7 * d,
                        rev(273.8777 + 1.64505e-5 * d), 5.20256,
                        0.048498 + 4.469e-9 * d, rev(19.8950 + 0.0830853001 * d)};
    Elements saturn = {rev(113.6634 + 2.38980e-5 * d), 2.4886 - 1.081e-7 * d,
                       rev(339.3939 + 2.97661e-5 * d), 9.55475,
                       0.055546 - 9.499e-9 * d, rev(316.9670 + 0.0334442282 * d)};

    eph.planets[0] = {"Ven", (float)planetAltitude(c, venus)};
    eph.planets[1] = {"Mar", (float)planetAltitude(c, mars)};
    eph.planets[2] = {"Jup", (float)planetAltitude(c, jupiter)};
    eph.planets[3] = {"Sat", (float)planetAltitude(c, saturn)};

    Serial.printf("[EPHEM] moon %d%% alt %.0f | Ven %.0f Mar %.0f Jup %.0f Sat %.0f\n",
                  eph.moonIllumPct, eph.moonAlt, eph.planets[0].alt,
                  eph.planets[1].alt, eph.planets[2].alt, eph.planets[3].alt);
}

// First moment after `from` when the Sun drops below DUSK_SUN_ALT, scanned
// in 10 minute steps. Returns 0 if not found within 12 hours (never happens
// at mid latitudes with the nautical -12 threshold).
time_t findDusk(time_t from)
{
    for (time_t t = from; t < from + 12 * 3600; t += 600)
    {
        SkyContext c;
        initContext(t, c);
        if (sunAltitude(c) < DUSK_SUN_ALT)
            return t;
    }
    return 0;
}
