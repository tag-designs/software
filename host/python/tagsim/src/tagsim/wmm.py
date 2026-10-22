"""Earth's field from the World Magnetic Model, through ``pygeomag``.

``pygeomag`` is a pure-Python port of NOAA's WMM reference code. It ships
the WMM2015, WMM2020 and WMM2025 coefficient files, needs nothing compiled,
and is maintained, so it installs wherever numpy does. tagsim takes the model
whose five-year epoch contains the flight date, so a 2023 flight uses WMM2020
as a compass calibrated then would have.

The field is evaluated once per log, at the EKF origin (``ORGN``) and the
flight's UTC date. Over a pattern box, a few hundred metres across and a
few hundred metres high, it varies by a few nanotesla, well below the
magnetometer's noise.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass
from datetime import datetime, timezone

#: Coefficient files shipped with pygeomag, by first year of validity.
_MODELS = {2015: "wmm/WMM_2015v2.COF", 2020: "wmm/WMM_2020.COF", 2025: "wmm/WMM_2025.COF"}


@dataclass
class EarthField:
    """The WMM field at one place and time."""

    model: str  #: coefficient file used
    decimal_year: float
    lat_deg: float
    lon_deg: float
    alt_m: float  #: as given; the WMM's height is above the WGS84 ellipsoid
    north_ut: float
    east_ut: float
    down_ut: float
    total_ut: float
    declination_deg: float
    inclination_deg: float

    @property
    def ned_ut(self) -> tuple[float, float, float]:
        return (self.north_ut, self.east_ut, self.down_ut)

    def as_dict(self) -> dict:
        return asdict(self)


def decimal_year(unix: float) -> float:
    """Calendar year with a fractional part, as the WMM takes it."""
    t = datetime.fromtimestamp(unix, timezone.utc)
    start = datetime(t.year, 1, 1, tzinfo=timezone.utc).timestamp()
    end = datetime(t.year + 1, 1, 1, tzinfo=timezone.utc).timestamp()
    return t.year + (unix - start) / (end - start)


def earth_field(lat_deg: float, lon_deg: float, alt_m: float, unix: float) -> EarthField:
    """The WMM field at a place and UTC time.

    Raises ``ValueError`` for a date before 2015 or after the newest model's
    five-year lifespan.
    """
    from pygeomag import GeoMag

    year = decimal_year(unix)
    epoch = max((e for e in _MODELS if e <= year), default=None)
    if epoch is None or year >= epoch + 5:
        raise ValueError(f"no WMM coefficients for {year:.2f}")
    r = GeoMag(coefficients_file=_MODELS[epoch]).calculate(
        glat=lat_deg, glon=lon_deg, alt=alt_m / 1000.0, time=year)
    return EarthField(model=_MODELS[epoch], decimal_year=year, lat_deg=lat_deg,
                      lon_deg=lon_deg, alt_m=alt_m, north_ut=r.x / 1000.0,
                      east_ut=r.y / 1000.0, down_ut=r.z / 1000.0, total_ut=r.f / 1000.0,
                      declination_deg=r.d, inclination_deg=r.i)


__all__ = ["EarthField", "earth_field", "decimal_year"]
