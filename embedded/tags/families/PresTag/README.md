# PresTag Family

This family contains the shared application code for PresTag pressure-log
targets that use the PresTagv3 board and LPS27 pressure sensor.

Family members:

- `PresTag`: exports converted pressure samples as `PresTagLog`.
- `PresTagRaw`: exports raw pressure pages as `PresTagRawLog`.

Both members report `PRESTAG` in monitor configuration. The target name and
firmware string identify the build variant; the protobuf payload identifies the
download log format.
