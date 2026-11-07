"""SWD capture and recovery of tags, from Python.

The binding of tagcore-rs's recovery module (host/libraries/tagcore-rs,
src/recovery); see host/python/tagcore-py/README.md.

- :mod:`tagcore.swd`: sessions that attach with the core halted at its reset
  vector (no firmware runs), memory access, and loader sessions on the
  external flash.
- :mod:`tagcore.recovery`: whole procedures in their own session: a capture,
  as ``tag-capture`` takes it, and rescue step 1, as
  ``tag-xflash rescue-erase`` runs it.

Every failure raises :class:`RecoveryError`.
"""

from tagcore._native import RecoveryError

# The native module names it _native.RecoveryError; it is tagcore's.
RecoveryError.__module__ = "tagcore"
from tagcore import recovery, swd

__all__ = ["RecoveryError", "recovery", "swd"]
