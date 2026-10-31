# Ultralight Tags User Guide

This guide covers the desktop tools used to program, configure, monitor,
calibrate, and visualize Ultralight Tags.

## Applications

- [Tag Programmer](apps/tagprogram.md): program a batch of tags and record
  the result.
- [Tag Monitor](apps/tagmonitor.md): inspect, configure, start, stop and
  download a tag.
- [Tag Calibrator](apps/tagcalibrate.md): calibrate a tag's magnetometer and
  accelerometer and write the values to the tag.
- [Tag View](apps/tagview.md): plot the SQLite logs downloaded from tags.

The Qt programs these replace (Qt Program, Qt Tag Monitor, Qt Calibrate and
SensorViz) are no longer in the packages; their pages are kept for one release
under **Legacy**.

## Common Workflows

- [First Setup](workflows/first-setup.md)
- [Firmware Programming](workflows/firmware-programming.md)
- [Calibration](workflows/calibration.md)
- [Data Download](workflows/data-download.md)

## Command-Line Tools

- [Command-Line Tools](cli/index.md): reference templates for the host CLI tools,
  including available options and fill-in sections for usage notes.

## Reference

- [SQLite Log Format](reference/sqlite-logs.md): table organization for `.db3`
  files created by tag download tools.
- [Documentation Guidelines](reference/documentation-guidelines.md): writing,
  source layout, screenshot, and image naming conventions for this guide.
