"""Whole recovery procedures, each in its own session.

``capture()`` takes a capture as ``tag-capture`` does and returns a
dictionary describing it. ``rescue_erase_external()`` is step 1 of a rescue,
as ``tag-xflash rescue-erase`` runs it; steps 2 (``tag-flash program --erase
plan``) and 3 (``tag-reset``) are separate, so that each can be watched
(docs/bench/capturing-a-tag.md, "Rescuing a tag")::

    import tagcore

    cap = tagcore.recovery.capture("captures", reason="before rescue",
                                   progress=print)
    tagcore.recovery.rescue_erase_external("AT25XE_PresTagv3-RW",
                                           capture=cap["dir"])
"""

from tagcore._native import capture, rescue_erase_external

__all__ = ["capture", "rescue_erase_external"]
