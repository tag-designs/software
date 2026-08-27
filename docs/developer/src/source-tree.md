# Source Tree

This view follows repository ownership. Source-adjacent Markdown remains in the
directory that owns the code; the developer portal stages those files under
`reference/` so they can be browsed without losing their source path.

## Top-Level Design

- [Design Index](reference/design/index.md)
- [Binary Datalogs](reference/design/binary-datalogs.md)
- [DataProcessing Post-Processing Application](reference/design/dataprocessing.md)
- [Windows Build Notes](reference/design/windows-build-notes.md)

## Embedded

- [Build Orientation](reference/embedded/design/build-orientation.md)
- [Boards](reference/embedded/boards/README.md)
- [Board Tools](reference/embedded/boards/tools/README.md)
- [Bases](reference/embedded/bases/README.md)
- [Base Build Sources](reference/embedded/bases/BUILD_SOURCES.md)
- [Base Notes](reference/embedded/bases/notes.md)

## Embedded Tags

- [Tags Overview](reference/embedded/tags/README.md)
- [Tag Build Sources](reference/embedded/tags/BUILD_SOURCES.md)
- [Monitor Interface](reference/embedded/tags/design/monitor_interface.md)
- [Restart Recovery](reference/embedded/tags/design/restart-recovery.md)
- [Custom Defines](reference/embedded/tags/design/custom-defines.md)
- [U375 Stop Support](reference/embedded/tags/design/u375-stop-support.md)
- [U375 Stop3 Terminal Sleep Plan](reference/embedded/tags/design/u375-stop3-terminal-sleep-plan.md)

## Embedded Tag Common

- [Common Firmware](reference/embedded/tags/common/README.md)
- [Core Runtime](reference/embedded/tags/common/core/README.md)
- [I2C Backend Model](reference/embedded/tags/common/core/i2c-backend-model.md)
- [LPTIM System Timer Design](reference/embedded/tags/common/core/design/lptim-system-timer.md)
- [LPTIM ARR-Match Stop Delay Design](reference/embedded/tags/common/core/design/stop-milliseconds-lptim-arr-delay.md)
- [Modules](reference/embedded/tags/common/modules/README.md)
- [RTC](reference/embedded/tags/common/rtc/README.md)
- [Sensors](reference/embedded/tags/common/sensors/README.md)
- [IMU Assumptions](reference/embedded/tags/common/sensors/imu/design/assumptions.md)
- [IMU Design Notes](reference/embedded/tags/common/sensors/imu/design_notes.md)
- [Magnetometer Sensors](reference/embedded/tags/common/sensors/mag/README.md)
- [Storage](reference/embedded/tags/common/storage/README.md)
- [Tests](reference/embedded/tags/common/test/README.md)

## Embedded Tag Targets

- [IMUTagNand](reference/embedded/tags/IMUTagNand/README.md)
- [IMUTagNand TODO](reference/embedded/tags/IMUTagNand/todo.md)
- [CompassTagAT25Breakout Notes](reference/embedded/tags/CompassTagAT25Breakout/design/notes.md)

## Embedded Tag Families

- [Families Overview](reference/embedded/tags/families/README.md)
- [BitPresTag](reference/embedded/tags/families/BitPresTag/README.md)
- [BitTagNG](reference/embedded/tags/families/BitTagNG/README.md)
- [BitTagNG Wakeup Note](reference/embedded/tags/families/BitTagNG/wakeup_note.md)
- [CompassTag](reference/embedded/tags/families/CompassTag/README.md)
- [IMUTag Family](reference/embedded/tags/families/IMUTag/README.md)
- [IMUTag Jitter-Free Sampling Timing Strategy](reference/embedded/tags/families/IMUTag/design/jitter-free-sampling-timing-reconstruction.md)
- [IMUTag Internal Header Checkpoints](reference/embedded/tags/families/IMUTag/design/internal-header-checkpoints.md)
- [PresTag](reference/embedded/tags/families/PresTag/README.md)

## Host

- [Host Overview](reference/host/README.md)
- [Host User Guide Screenshot Automation](reference/host/docs/design/screenshot-automation.md)
- [Applications](reference/host/applications/README.md)
- [Command Line](reference/host/commandline/README.md)
- [Common Host Code](reference/host/common/README.md)
- [Libraries](reference/host/libraries/README.md)
- [TagCore](reference/host/libraries/tagcore/README.md)
- [TagCore Design Index](reference/host/libraries/tagcore/design/index.md)
- [TagCore Python Interface Design](reference/host/libraries/tagcore/design/python-interface.md)
- [SensorAnalysis](reference/host/libraries/sensoranalysis/README.md)
- [SensorUI](reference/host/libraries/sensorui/README.md)

## Host Applications

- [QtCalibrate](reference/host/applications/qtcalibrate/README.md)
- [QtMonitor](reference/host/applications/qtmon/README.md)
- [QtMonitor Tag Configuration](reference/host/applications/qtmon/tagconfiguration/README.md)
- [QtMonitor Screenshot Automation](reference/host/docs/design/qtmonitor-screenshot-automation.md)
- [QtMonitor Fixture Captures](reference/host/docs/fixtures/qtmonitor/README.md)
- [SensorViz](reference/host/applications/sensorviz/README.md)
- [SensorViz Sample Logs](reference/host/docs/fixtures/sensorviz/README.md)
- [SensorViz Roadmap](reference/host/applications/sensorviz/design/roadmap.md)
- [SensorViz Screenshot Capture Plan](reference/host/applications/sensorviz/design/screenshot-capture-plan.md)

## Proto

- [Shared Protobuf Definitions](reference/proto/README.md)
