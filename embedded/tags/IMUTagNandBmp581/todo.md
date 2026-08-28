# IMUTagNandBmp581 Bring-Up TODO

## Near-Term Validation

- Investigate the GD5F2GM7RE `B9h` extreme-low-power command and the wake
  sequence/timing needed before the shared NAND driver can use it.
- Exercise the NAND erase path with a provisioned map present.
- Confirm the tag refuses to start collection when the NAND map is absent.
- Test page-cache write behavior across NAND page boundaries.
- Verify normal reprogramming and erase flows preserve calibration, NAND map,
  and dedicated configuration pages.
- Test behavior with simulated or forced factory bad-block markers.

## Read/Download Robustness

- Handle ECC status on NAND page reads.
- Provide a "bad page" marker or equivalent read result so host download code
  can skip unreadable pages instead of treating the whole download as failed.
- Decide where bad-page observations live after runtime reads: transient RAM
  table, debug log only, persisted table, or appended download metadata.
- Add a way to inspect bad-page information during download, either by dumping
  the bad-page table or exposing a monitor/debug command.
- Confirm downloader behavior when sparse pages are skipped, including sample
  count, timestamp continuity, and log metadata.

## Flash Map And Bad-Block Handling

- Keep first-boot map provisioning read-only with respect to NAND factory
  markers.
- Confirm logical-to-physical map validation rejects erased, unsorted, or
  out-of-range entries.
- Confirm erase always checks physical bad-block markers before issuing NAND
  block erase.
- Decide whether runtime program or erase failures should retire blocks, report
  an error only, or both.

## Host And Diagnostics

- Keep qtmonitor line-buffering behavior under longer debug streams.
- Consider quieting duplicate NAND ID logs once bring-up stabilizes.
- Add host-side checks that clearly distinguish absent map, bad NAND ID,
  unreadable page, and erased/no-data cases.
