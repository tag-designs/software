# PresTag Family

This family contains the shared application code for PresTag pressure-log
targets that use the PresTagv3 board and LPS27 pressure sensor.

Family members:

- `PresTag`: exports converted pressure samples as `PresTagLog`.
- `PresTagRaw`: exports raw pressure pages as `PresTagRawLog`.

Both members report `PRESTAG` in monitor configuration. The target name and
firmware string identify the build variant; the protobuf payload identifies the
download log format.

## Design notes

- [Power and Schedule Test Plan](design/power-test-plan.md): hardware-in-the-loop
  procedure for sample period, start, stop and hibernation. Explains the 10 s
  sleep-mode boundary in `Running()`, why an unbiased power window is 60 sample
  periods rather than one, and the linear average-current law used to estimate
  battery lifetime.
- [Power and Schedule Test Report](design/power-test-report.md): results form and
  baseline table for the above. Not yet executed.
