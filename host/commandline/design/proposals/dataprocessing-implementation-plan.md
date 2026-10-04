---
type: proposal
status: historical
summary: The original refactoring steps and implementation phases for the dataprocessing CLI, kept for the record now that the tool is built.
---

# DataProcessing Implementation Plan

This is the step list from the original
[DataProcessing design](../dataprocessing.md), cut out verbatim once the tool
was built (`host/commandline/dataprocessing.cc`, added 2026-08-27). The CLI,
`ProcessingRun` provenance, and the `compass-calibrated` and
`compass-orientation` processors exist; SensorViz support for augmented files
and the fixture tests do not. Do not quote this as current.

## Refactoring Plan

1. Keep the `magcal` solver in qtcalibrate for now.
2. Move or add UI-free conversion helpers in `sensoranalysis` for all
   calibration constants that must be shared by qtcalibrate, SensorViz, and
   DataProcessing.
3. Promote any remaining CompassTag orientation helpers from SensorViz into
   `sensoranalysis` when they are algorithmic rather than presentational.
4. Keep SQLite read/write helpers outside `sensoranalysis` so that library
   remains independent of database format and application policy.
5. Add DataProcessing as a host command-line application that links
   `sensoranalysis`, SQLite, and the appropriate host common utilities.
6. Update SensorViz later to distinguish raw streams, materialized processed
   streams, and live display-derived streams.

## Implementation Phases

1. Create `dataprocessing` CLI skeleton with argument parsing, input/output
   path validation, database copy, and `--dry-run`.
2. Add `ProcessingRun` table creation and provenance writing.
3. Implement CompassTag input discovery and validation.
4. Implement `compass-calibrated`.
5. Implement `compass-orientation`.
6. Add tests against the CompassTag fixture and SQLite integrity checks.
7. Document maintainer usage and processor configuration.
8. Plan SensorViz augmented-file behavior after the output database contract is
   stable.
