# Developer Architecture & Design Index

Welcome to the Developer Design Index. This directory contains technical specifications, design decisions, and system notes for the tag-designs codebase. 

> [!NOTE]
> For user-facing application manuals and deployment workflows, please refer to the [User Guide](file:///host/docs/README.md) (built via MkDocs).

---

## 1. Cross-Cutting Design

*   [**Binary Datalogging Design**](binary-datalogs.md): System standard for shared binary log structure alignment, naming conventions, and nanopb constraints.
*   [**Windows Build Notes**](windows-build-notes.md): Reference notes on MSVC compiler setups, vcpkg static dependencies, and developer environment paths.

---

## 2. Embedded & Firmware Platform

*   [**Embedded Build Orientation**](../embedded/design/build-orientation.md): Layout overview of boards, proto-c, base firmwares, and tag targets.
*   [**Custom Compiler Definitions**](../embedded/tags/design/custom-defines.md): Complete list of customizable flags, timer settings, and MCU preprocessor defines.
*   [**Restart Recovery Design**](../embedded/tags/design/restart-recovery.md): Specification for state machine preservation, register resets, and low-power recovery cycles.
*   [**IMUTagU375 Bring-Up Closeout**](../embedded/design/imutag-u375-bringup-closeout.md): Parked STM32U375 investigation notes, useful findings, and unresolved blockers.

---

## 3. Sensors & Hardware Drivers

*   [**IMU Design Assumptions**](../embedded/tags/common/sensors/imu/design/assumptions.md): Synchronized FIFO sampling, LSM6 time-slot pairings, and environmental sensor sparse sampling.
*   [**IMU Performance Notes**](../embedded/tags/IMUTagBreakout/design/performance-notes.md): Tag-specific timing analysis, FIFO limits, and bandwidth profiling.
*   [**CompassTag Breakout Design Notes**](../embedded/tags/CompassTagAT25Breakout/design/notes.md): Board layout, power domains, SPI bus layout, and calibration preservation.

---

## 4. Host Applications

*   [**SensorViz Roadmap**](../host/applications/sensorviz/design/roadmap.md): Display preference policies, plotting roadmaps, and SQLite reader features.
