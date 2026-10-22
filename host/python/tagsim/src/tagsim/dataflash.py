"""Reader for ArduPilot DataFlash (``.bin``) logs.

The format describes itself: every message starts with the bytes ``A3 95``
and a one-byte type, and ``FMT`` messages (type 128) give each type's name,
length, field format characters and column names. This reader scans the file
once, then decodes the message types asked for into numpy structured arrays,
so no pymavlink is needed.

Scaled formats (``c``, ``C``, ``e``, ``E``, ``L``) are returned already
scaled, as float64. Bytes that do not start a known message are skipped one at
a time, which resynchronises after a corrupt record.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np

HEAD = b"\xa3\x95"
FMT_TYPE = 128

# Format character -> (numpy dtype, scale applied on decode or None).
_FORMATS: dict[str, tuple[str, float | None]] = {
    "a": ("(32,)<i2", None),
    "b": ("i1", None),
    "B": ("u1", None),
    "h": ("<i2", None),
    "H": ("<u2", None),
    "i": ("<i4", None),
    "I": ("<u4", None),
    "f": ("<f4", None),
    "d": ("<f8", None),
    "n": ("S4", None),
    "N": ("S16", None),
    "Z": ("S64", None),
    "c": ("<i2", 0.01),
    "C": ("<u2", 0.01),
    "e": ("<i4", 0.01),
    "E": ("<u4", 0.01),
    "L": ("<i4", 1e-7),
    "M": ("u1", None),
    "q": ("<i8", None),
    "Q": ("<u8", None),
}


@dataclass
class MessageFormat:
    """One ``FMT`` record: how a message type is laid out."""

    type: int
    length: int  #: whole message, including the 3-byte header
    name: str
    format: str
    columns: list[str]

    def dtype(self) -> np.dtype:
        """The packed payload layout, little-endian, without the header."""
        return np.dtype([(c, _FORMATS[f][0]) for c, f in zip(self.columns, self.format)])


class DataFlashLog:
    """A DataFlash log, scanned once; messages decoded on request.

    >>> log = DataFlashLog("00000173.BIN")
    >>> xkq = log.messages("XKQ1")      # dict of column -> numpy array
    """

    def __init__(self, path: str | Path):
        self.path = Path(path)
        self._buf = self.path.read_bytes()
        self.formats: dict[str, MessageFormat] = {}
        self._offsets: dict[int, list[int]] = {}
        self._scan()

    def _scan(self) -> None:
        buf = self._buf
        lengths: dict[int, int] = {FMT_TYPE: 89}
        offsets = self._offsets
        n = len(buf)
        pos = buf.find(HEAD)
        while 0 <= pos < n - 3:
            if buf[pos] != 0xA3 or buf[pos + 1] != 0x95:
                pos = buf.find(HEAD, pos + 1)
                continue
            t = buf[pos + 2]
            length = lengths.get(t)
            if length is None or pos + length > n:
                pos = buf.find(HEAD, pos + 1)
                continue
            if t == FMT_TYPE:
                body = buf[pos + 3:pos + length]
                ftype, flen = body[0], body[1]
                name = body[2:6].rstrip(b"\0").decode("ascii", "replace")
                fmt = body[6:22].rstrip(b"\0").decode("ascii", "replace")
                cols = body[22:86].rstrip(b"\0").decode("ascii", "replace").split(",")
                if all(c in _FORMATS for c in fmt) and len(cols) == len(fmt):
                    self.formats[name] = MessageFormat(ftype, flen, name, fmt, cols)
                    lengths[ftype] = flen
            else:
                offsets.setdefault(t, []).append(pos)
            pos += length

    def has(self, name: str) -> bool:
        """True when the log defines ``name`` and holds at least one."""
        f = self.formats.get(name)
        return f is not None and bool(self._offsets.get(f.type))

    def messages(self, name: str) -> dict[str, np.ndarray]:
        """Every ``name`` message, as column name -> array, in file order.

        Raises KeyError when the log has no such message type.
        """
        f = self.formats[name]
        offs = np.asarray(self._offsets.get(f.type, []), dtype=np.int64)
        dt = f.dtype()
        raw = np.frombuffer(self._buf, dtype=np.uint8)
        if len(offs):
            idx = offs[:, None] + 3 + np.arange(dt.itemsize)[None, :]
            rows = raw[idx].copy().view(dt).reshape(-1)
        else:
            rows = np.zeros(0, dtype=dt)
        out: dict[str, np.ndarray] = {}
        for col, ch in zip(f.columns, f.format):
            scale = _FORMATS[ch][1]
            out[col] = rows[col].astype(np.float64) * scale if scale else rows[col]
        return out
