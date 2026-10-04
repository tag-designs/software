---
type: decision
status: accepted
summary: Per-session facts, starting with the RV3028 factory EEOffset, are stored with the stored configuration so they are rewritten for every session.
---

# 0020. Session facts are stored with the stored configuration

Date: 2026-10-01

Item 5 of the plan agreed on 2026-10-01 for rebuilding a download from an SWD capture, one of the items that make future releases self-describing. Cut verbatim from [Offline Log Reconstruction](../investigations/2026-10-offline-log-reconstruction.md), "Decisions and plan", which holds the fw-v0.0.3 analysis behind it.

## Decision

Per-session facts are stored **with the stored configuration (`sconfig`)**. The
stored configuration is erased when the data are cleared and written when the
tag is started, so these facts are rewritten for each session and are never
left describing an earlier one. The record holds:
- **the RV3028 factory EEOffset**: the raw 9-bit steps and the derived ppm, as
  the IMUTag timing design already specifies. `infoAck()` reports the stored
  value instead of reading it live. It is a per-chip constant, read from the
  RV3028 at start and kept beside the configuration it applies to;
- the effective sample period, ODR and ranges, as values rather than enums;
- ideally the nanopb-encoded `Config` itself, which would remove the
  per-family `readConfig()` mapping from the offline path.

This is the session superblock of Field Data Extraction, now given a place. Its
format should be designed together with item 4's record.
