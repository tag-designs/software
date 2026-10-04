---
type: decision
status: accepted
summary: A capture also records the RV3028 EEOffset (read by an SRAM-resident routine), each used NAND page read through on-die ECC with its status, and internal-flash double-words that raise ECC faults.
---

# 0019. Captures close the offline-rebuild gaps: RTC offset, NAND ECC status, internal-flash ECC faults

Date: 2026-10-01

Item 2 of the plan agreed on 2026-10-01 for rebuilding a download from an SWD capture; it needs no firmware change. Cut verbatim from [Offline Log Reconstruction](../investigations/2026-10-offline-log-reconstruction.md), "Decisions and plan", which holds the fw-v0.0.3 analysis behind it.

## Decision

- **For tags in the field now, read the RV3028's EEOffset over I2C.** A small
  SRAM-resident routine, run through the capture library's SRAM-call layer
  (the same mechanism as the flash loaders), can do this without booting the
  tag. Item 5 makes this unnecessary for future releases.
- **For NAND, read each used page through on-die ECC and record its status**,
  as well as the raw page.
- **For internal flash, record which double-words raise ECC faults.** The
  firmware truncates the log at the first one.
