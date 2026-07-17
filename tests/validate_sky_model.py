#!/usr/bin/env python3
"""Offline validation of SkyModel::ComputeSun (src/core/SkyLighting.cpp).

Faithful double-precision port of the C++ orbital model, verified against the
formula's own closed-form consequences: solstice declination, noon-altitude
identity, sunrise/sunset hour angle (cos H0 = -tan(lat) tan(decl)), azimuth
quadrants, and polar day/night behavior.

Scope, stated honestly: this witnesses that the IMPLEMENTATION matches the
standard geometric solar-position formula and behaves sanely at the edges.
The model itself is geometric only (no equation of time, no atmospheric
refraction, no solar-disc radius), which is the intended fidelity for a game
sky. Float(32) bit-exactness vs the C++ is not claimed; the identities hold
to far tighter tolerance than one game frame of sun motion.

The live NiNode write this feeds ([Sky] Enable + [Orbit] MoveSun, both OFF)
is game-bound: docs/VALIDATION-PROTOCOL.md has the in-game steps.
"""

import math
import sys

DEG = math.pi / 180.0


def compute_sun(day_of_year, hour, axial_tilt, latitude, summer_solstice_day):
    """Port of SkyModel::ComputeSun. Returns (altitude, azimuth, dx, dy, dz)."""
    eps = axial_tilt * DEG
    lat = latitude * DEG

    day_angle = 2.0 * math.pi * (day_of_year - summer_solstice_day) / 365.0
    decl = eps * math.cos(day_angle)

    H = (hour - 12.0) * 15.0 * DEG

    sin_alt = math.sin(lat) * math.sin(decl) + \
        math.cos(lat) * math.cos(decl) * math.cos(H)
    sin_alt = max(-1.0, min(1.0, sin_alt))
    alt = math.asin(sin_alt)

    # atan2 azimuth on the horizontal (east, north) components, matching the
    # C++ (exact at the noon pole; no epsilon guard).
    east = -math.cos(decl) * math.sin(H)
    north = math.sin(decl) * math.cos(lat) - \
        math.cos(decl) * math.sin(lat) * math.cos(H)
    az = math.atan2(east, north)
    if az < 0.0:
        az += 2.0 * math.pi

    return alt, az, east, north, sin_alt


def declination(day_of_year, axial_tilt, summer_solstice_day):
    day_angle = 2.0 * math.pi * (day_of_year - summer_solstice_day) / 365.0
    return axial_tilt * DEG * math.cos(day_angle)


# Skyrim Sky.ini defaults (src/core/SkyLighting.h SkyConfig).
TILT, LAT, SOLSTICE = 23.4, 35.0, 173.0
EQUINOX = SOLSTICE + 365.0 / 4.0   # decl crosses zero a quarter period later

_passed = 0
_failed = 0


def check(label, ok, detail=""):
    global _passed, _failed
    if ok:
        _passed += 1
        print(f"  PASS  {label}")
    else:
        _failed += 1
        print(f"  FAIL  {label}  {detail}")


def altitude_at(hour, doy=SOLSTICE, lat=LAT):
    return compute_sun(doy, hour, TILT, lat, SOLSTICE)[0] / DEG


def find_crossing(lo, hi, doy, lat):
    """Bisect the altitude zero crossing between two hours."""
    for _ in range(60):
        mid = 0.5 * (lo + hi)
        if altitude_at(lo, doy, lat) * altitude_at(mid, doy, lat) <= 0.0:
            hi = mid
        else:
            lo = mid
    return 0.5 * (lo + hi)


print("[formula identities]")
d = declination(SOLSTICE, TILT, SOLSTICE) / DEG
check("declination at summer solstice = +tilt", abs(d - TILT) < 1e-9, f"{d}")
d = declination(SOLSTICE + 182.5, TILT, SOLSTICE) / DEG
check("declination half a year on = -tilt", abs(d + TILT) < 1e-9, f"{d}")

ok = True
for doy in (SOLSTICE, EQUINOX, SOLSTICE + 182.5, 40.0, 300.0):
    decl = declination(doy, TILT, SOLSTICE) / DEG
    alt = compute_sun(doy, 12.0, TILT, LAT, SOLSTICE)[0] / DEG
    want = 90.0 - LAT + decl          # asin(cos(lat-decl)), lat > decl always here
    if abs(alt - want) > 1e-6:
        ok = False
check("noon altitude = 90 - lat + decl (5 days)", ok)

ok = all(abs(altitude_at(12 - h) - altitude_at(12 + h)) < 1e-9 for h in (2, 4, 6))
check("altitude symmetric about noon", ok)

ok = True
for hour in (0.0, 5.5, 12.0, 17.25, 23.0):
    alt, az, dx, dy, dz = compute_sun(EQUINOX, hour, TILT, LAT, SOLSTICE)
    if abs(dx * dx + dy * dy + dz * dz - 1.0) > 1e-9 or abs(dz - math.sin(alt)) > 1e-9:
        ok = False
check("incident vector unit length, dirZ = sin(alt)", ok)

print("[sunrise / sunset closed form]")
sr = find_crossing(0.0, 12.0, EQUINOX, LAT)
ss = find_crossing(12.0, 24.0, EQUINOX, LAT)
check("equinox sunrise at 6.00", abs(sr - 6.0) < 0.01, f"{sr:.4f}")
check("equinox sunset at 18.00", abs(ss - 18.0) < 0.01, f"{ss:.4f}")

decl = declination(SOLSTICE, TILT, SOLSTICE)
H0 = math.acos(max(-1.0, min(1.0, -math.tan(LAT * DEG) * math.tan(decl)))) / DEG
sunrise_cf = 12.0 - H0 / 15.0
alt_at_cf = altitude_at(sunrise_cf)
check("altitude = 0 at closed-form sunrise hour", abs(alt_at_cf) < 1e-6,
      f"{alt_at_cf:.6f} deg at hour {sunrise_cf:.4f}")

sr = find_crossing(0.0, 12.0, SOLSTICE, LAT)
ss = find_crossing(12.0, 24.0, SOLSTICE, LAT)
day_len = ss - sr
check("summer-solstice day length matches 2*H0/15",
      abs(day_len - 2.0 * H0 / 15.0) < 0.01, f"{day_len:.4f} vs {2*H0/15:.4f}")

wsr = find_crossing(0.0, 12.0, SOLSTICE + 182.5, LAT)
wss = find_crossing(12.0, 24.0, SOLSTICE + 182.5, LAT)
check("winter day length = 24 - summer day length",
      abs((wss - wsr) - (24.0 - day_len)) < 0.02, f"{wss - wsr:.4f}")

print("[azimuth semantics]")
az_noon = compute_sun(SOLSTICE, 12.0, TILT, LAT, SOLSTICE)[1] / DEG
check("noon azimuth = 180 (due south, lat > decl)", abs(az_noon - 180.0) < 0.1,
      f"{az_noon:.3f}")
az_sr = compute_sun(EQUINOX, 6.0, TILT, LAT, SOLSTICE)[1] / DEG
az_ss = compute_sun(EQUINOX, 18.0, TILT, LAT, SOLSTICE)[1] / DEG
check("equinox sunrise azimuth = 90 (east)", abs(az_sr - 90.0) < 1.0, f"{az_sr:.3f}")
check("equinox sunset azimuth = 270 (west)", abs(az_ss - 270.0) < 1.0, f"{az_ss:.3f}")

print("[polar and edge behavior]")
alt_midnight = compute_sun(SOLSTICE, 0.0, TILT, 80.0, SOLSTICE)[0] / DEG
check("lat 80 summer midnight sun (altitude > 0)", alt_midnight > 0.0,
      f"{alt_midnight:.3f}")
alt_polar_noon = compute_sun(SOLSTICE + 182.5, 12.0, TILT, 80.0, SOLSTICE)[0] / DEG
check("lat 80 winter polar night (noon altitude < 0)", alt_polar_noon < 0.0,
      f"{alt_polar_noon:.3f}")
alt_eq = compute_sun(EQUINOX, 12.0, TILT, 0.0, SOLSTICE)[0] / DEG
check("equator equinox noon at zenith", abs(alt_eq - 90.0) < 0.1, f"{alt_eq:.4f}")

ok = True
h = 0.0
while h < 24.0:
    for doy in range(0, 366, 30):
        for lat in (-80.0, -35.0, 0.0, 35.0, 80.0):
            vals = compute_sun(float(doy), h, TILT, lat, SOLSTICE)
            if any(math.isnan(v) or math.isinf(v) for v in vals):
                ok = False
    h += 0.5
check("no NaN/Inf across full sweep (48 hours x 13 days x 5 latitudes)", ok)

print(f"\n{_passed} passed, {_failed} failed")
sys.exit(1 if _failed else 0)
