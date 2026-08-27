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
*   [**Tag Monitor Interface**](../embedded/tags/design/monitor_interface.md): Reference for the STM32L4 DebugMonitor path and STM32U3 shared-memory monitor path.
*   [**Restart Recovery Design**](../embedded/tags/design/restart-recovery.md): Specification for state machine preservation, register resets, and low-power recovery cycles.
*   [**STM32U375 Stop-Mode Support**](../embedded/tags/design/u375-stop-support.md): Current returned-idle STOP policy, monitor attach guard, and scoped flash/SPI low-power behavior for U375 tag targets.
*   [**STM32U375 Stop3 Terminal Sleep Plan**](../embedded/tags/design/u375-stop3-terminal-sleep-plan.md): Plan for replacing IMUTagNand terminal Standby with Stop3 while preserving the STM32L432 Standby path.
*   [**LPTIM System Timer Design**](../embedded/tags/common/core/design/lptim-system-timer.md): Proposal for an STM32 LPTIM3/LPTIM4-backed ChibiOS ST driver and a Sleep-mode fallback when timer alarms are active.
*   [**LPTIM ARR-Match Stop Delay Design**](../embedded/tags/common/core/design/stop-milliseconds-lptim-arr-delay.md): Proposed STM32L432 `stopMilliseconds()` cleanup using LPTIM1 autoreload match, explicit 1024 Hz tick conversion, and spurious-wake filtering.

---

## 3. Sensors & Hardware Drivers

*   [**IMUTag Jitter-Free Sampling Timing Strategy**](../embedded/tags/families/IMUTag/design/jitter-free-sampling-timing-reconstruction.md): Plan for smooth RV-3028-derived sampling, STM32 RTC real-time correction, and downloadable metadata for corrected timing reconstruction.
*   [**IMU Design Assumptions**](../embedded/tags/common/sensors/imu/design/assumptions.md): Synchronized FIFO sampling, LSM6 time-slot pairings, and environmental sensor sparse sampling.
*   [**CompassTag Breakout Design Notes**](../embedded/tags/CompassTagAT25Breakout/design/notes.md): Board layout, power domains, SPI bus layout, and calibration preservation.

---

## 4. Host Applications

*   [**Host User Guide Screenshot Automation**](../host/docs/design/screenshot-automation.md): Pilot plan for deterministic Qt application screenshots and generated annotations, starting with `qtcalibrate`.
*   [**QtMonitor Screenshot Automation**](../host/docs/design/qtmonitor-screenshot-automation.md): Design for fake-tag fixtures, per-tag default configuration screenshots, and representative state screenshots for `qtmonitor`.
*   [**DataProcessing Post-Processing Application**](dataprocessing.md): Design for a host CLI that copies SQLite logs, materializes calibrated/derived streams, and records processing provenance for analysis outside SensorViz.
*   [**SensorViz Roadmap**](../host/applications/sensorviz/design/roadmap.md): Display preference policies, plotting roadmaps, and SQLite reader features.
*   [**SensorViz Screenshot Capture Plan**](../host/applications/sensorviz/design/screenshot-capture-plan.md): Strategy for sample SQLite fixtures, menu/dialog captures, display customization screenshots, and tag-specific SensorViz documentation.

---

## 5. Host Libraries

*   [**TagCore Python Interface Design**](../host/libraries/tagcore/design/python-interface.md): Proposed Python API, protobuf/native boundary, shared download workflow, packaging, and test strategy.
