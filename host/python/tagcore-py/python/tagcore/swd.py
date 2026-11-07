"""SWD recovery sessions and loader sessions.

A session attaches under reset with the core halted at its reset vector, so
the tag's firmware never runs, and owns the base until it closes::

    import tagcore

    with tagcore.swd.open() as s:              # the only base on USB
        print(s.mcu_name, s.uid, s.attach["pc"])
        regs = s.read_region("backup_regs")
        with s.external_flash("AT25XE_PresTagv3") as xf:
            print(hex(xf.jedec), xf.size)
            data = xf.read(0, xf.size, progress=lambda done, total: None)
    # leaving the block resets the tag (exit="reset"), as tag-capture does

A monitor session (tag-info, Tag Monitor) cannot use the base while a
session is open. Downloading a loader overwrites the start of SRAM1: read
SRAM first if it matters.
"""

from tagcore._native import ExternalFlash, Session, bases, open

__all__ = ["ExternalFlash", "Session", "bases", "open"]
