---
type: decision
status: accepted
summary: Each firmware package ships a build-time layout descriptor, so a capture plus the package decodes without the source checkout.
---

# 0017. Firmware packages ship a build-time layout descriptor

Date: 2026-10-01

Item 3 of the plan agreed on 2026-10-01 for rebuilding a download from an SWD capture, one of the items that make future releases self-describing. Cut verbatim from [Offline Log Reconstruction](../investigations/2026-10-offline-log-reconstruction.md), "Decisions and plan", which holds the fw-v0.0.3 analysis behind it.

## Decision

It holds:
- the symbol addresses and section bounds already in the ELF;
- for every stored struct, `sizeof` and member offsets, emitted by a
  build-time C program compiled with the target's flags. The ELF has no DWARF,
  and the short-enum ABI must be captured;
- compile-time constants: samples per page, stride, cadence, sample period;
- scale factors, and whether each is a multiply or a divide;
- end-of-log and missing-value rules, as named rule IDs;
- the per-family `BackupState` word map;
- the `.proto` files, or a `FileDescriptorSet`;
- the decoder name.

With it, capture plus package is enough for every tag, and the source checkout
is no longer needed.
