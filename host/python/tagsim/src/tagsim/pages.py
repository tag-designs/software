"""IMUTag log pages, exactly as ``t_ImuTagDataLog`` in include/imutag_log_format.h.

A page is 2048 bytes: an 8-byte header (``int32 epoch``, ``uint16 millis``
holding 1/1024 s ticks, ``int16 rawtemp`` in 0.01 C) and 15 superframes of
ten 12-byte IMU samples plus one 16-byte auxiliary slot (pressure and
magnetometer, ``float``). A missing auxiliary value is the exact quiet NaN
``0x7fc00000``, the only pattern the host decoder treats as missing; a
superframe never written is all ``0xFF`` and ends the page's data.

What the firmware serves for each page (``data_logAck()`` in
``embedded/tags/families/IMUTag/src/datalog.c``) is an Ack whose
``imu_raw_data_log`` carries the page epoch, the page ticks OR'd with the
checkpoint's flags, the temperature and the page bytes. Checkpoints cover
groups of ``IMUTAG_CHECKPOINT_PAGES`` (8) pages, and only the first page of a
group carries flags, so a segment boundary (a ``RESYNC``) always falls on a
group's first page.
"""

from __future__ import annotations

import base64
from dataclasses import dataclass

import numpy as np

PAGE_BYTES = 2048
SAMPLES_PER_SUPERFRAME = 10
SUPERFRAMES_PER_PAGE = 15
SAMPLES_PER_PAGE = SAMPLES_PER_SUPERFRAME * SUPERFRAMES_PER_PAGE
CHECKPOINT_PAGES = 8
TICKS_PER_SECOND = 1024

HEADER_MILLIS_MASK = 0x03FF
HEADER_RESYNC = 0x0400
HEADER_RESYNC_STORAGE_SKIP = 0x0800
HEADER_RESTART_RECOVERY = 0x1000

MISSING_AUX_BITS = 0x7FC00000

HEADER_DTYPE = np.dtype([("epoch", "<i4"), ("millis", "<u2"), ("rawtemp", "<i2")])
SUPERFRAME_DTYPE = np.dtype([("imu", "<i2", (SAMPLES_PER_SUPERFRAME, 6)), ("aux", "<u4", (4,))])
PAGE_DTYPE = np.dtype([("header", HEADER_DTYPE), ("frames", SUPERFRAME_DTYPE, (SUPERFRAMES_PER_PAGE,))])
assert HEADER_DTYPE.itemsize == 8 and SUPERFRAME_DTYPE.itemsize == 136
assert PAGE_DTYPE.itemsize == PAGE_BYTES


@dataclass
class Segment:
    """One stretch of continuous sampling.

    ``imu`` is (n, 6) int16 in stored axes, n a multiple of ten; ``aux`` is
    (n / 10, 4) float32, NaN where a value is missing. ``start_unix`` is the
    UTC time of the first sample, ``odr_hz`` the sample rate. ``flags`` are
    the header flags its first page's checkpoint carries; the first segment
    of a log has none, and a later one needs at least ``HEADER_RESYNC``.
    ``rawtemp_c`` is the temperature written in each page's header.
    """

    imu: np.ndarray
    aux: np.ndarray
    start_unix: float
    odr_hz: int
    rawtemp_c: np.ndarray | float = 0.0
    flags: int = 0


@dataclass
class Page:
    """One page as the tag would serve it."""

    index: int  #: logical page index (download index)
    epoch: int
    ticks: int
    flags: int  #: checkpoint flags in the Ack's millisecond field
    temperature_c: float
    data: bytes

    @property
    def millisecond(self) -> int:
        return self.ticks | self.flags


def aux_bits(aux: np.ndarray) -> np.ndarray:
    """float32 auxiliary values as stored words, NaN as exactly 0x7fc00000."""
    a = np.asarray(aux, dtype=np.float32)
    bits = a.view(np.uint32).copy()
    bits[np.isnan(a)] = MISSING_AUX_BITS
    return bits


def pack(segments: list[Segment]) -> list[Page]:
    """Pages for a log of one or more segments.

    Each segment after the first starts on the first page of the next
    checkpoint group. Indices in between are never written, as after a
    storage skip, and a downloader steps over them. The last page of every
    segment is left partly erased when its samples do not fill it.
    """
    pages: list[Page] = []
    index = 0
    for k, seg in enumerate(segments):
        imu = np.asarray(seg.imu, dtype=np.int16)
        if len(imu) % SAMPLES_PER_SUPERFRAME:
            raise ValueError("a segment holds whole superframes")
        n_sf = len(imu) // SAMPLES_PER_SUPERFRAME
        if len(seg.aux) != n_sf:
            raise ValueError("one auxiliary slot per superframe")
        if k > 0:
            if not seg.flags & HEADER_RESYNC:
                raise ValueError("a later segment must carry HEADER_RESYNC")
            index = -(-index // CHECKPOINT_PAGES) * CHECKPOINT_PAGES
        elif seg.flags:
            raise ValueError("the first segment carries no flags")
        frames = np.zeros(n_sf, dtype=SUPERFRAME_DTYPE)
        frames["imu"] = imu.reshape(n_sf, SAMPLES_PER_SUPERFRAME, 6)
        frames["aux"] = aux_bits(seg.aux)
        n_pages = -(-n_sf // SUPERFRAMES_PER_PAGE)
        temps = np.broadcast_to(np.asarray(seg.rawtemp_c, dtype=np.float64), (n_pages,))
        for p in range(n_pages):
            page = np.frombuffer(bytearray(b"\xff" * PAGE_BYTES), dtype=PAGE_DTYPE)
            chunk = frames[p * SUPERFRAMES_PER_PAGE:(p + 1) * SUPERFRAMES_PER_PAGE]
            page["frames"][0, :len(chunk)] = chunk
            t = seg.start_unix + p * SAMPLES_PER_PAGE / seg.odr_hz
            epoch = int(np.floor(t))
            ticks = min(int((t - epoch) * TICKS_PER_SECOND), TICKS_PER_SECOND - 1)
            rawtemp = int(np.clip(np.rint(temps[p] * 100.0), -32768, 32767))
            page["header"][0] = (epoch, ticks, rawtemp)
            flags = seg.flags if p == 0 else 0
            if flags and index % CHECKPOINT_PAGES:
                raise AssertionError("flags off a checkpoint boundary")
            pages.append(Page(index, epoch, ticks, flags, rawtemp * 0.01, page.tobytes()))
            index += 1
    return pages


def unpack(data: bytes) -> tuple[tuple[int, int, int], np.ndarray, np.ndarray]:
    """Header, IMU samples and auxiliary words of the written superframes."""
    page = np.frombuffer(data, dtype=PAGE_DTYPE)[0]
    frames = page["frames"]
    raw = np.frombuffer(data[8:], dtype=np.uint8).reshape(SUPERFRAMES_PER_PAGE, -1)
    written = ~np.all(raw == 0xFF, axis=1)
    n = int(np.argmin(written)) if not written.all() else SUPERFRAMES_PER_PAGE
    h = page["header"]
    return ((int(h["epoch"]), int(h["millis"]), int(h["rawtemp"])),
            frames["imu"][:n].reshape(-1, 6), frames["aux"][:n])


def ack_dict(page: Page) -> dict:
    """The page as tag_dwnld.py writes it: the Ack in protobuf JSON."""
    log = {"epoch": page.epoch, "temperature": page.temperature_c,
           "samples": base64.b64encode(page.data).decode("ascii")}
    if page.millisecond:
        log["millisecond"] = page.millisecond
    return {"err": "OK", "imu_raw_data_log": log}
