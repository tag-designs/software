# Tag Monitor

Tag Monitor connects to a tag through a USB base and shows the information
needed to prepare, check, run, and recover a data logging experiment. Use it to
verify the tag identity and firmware, synchronize the tag clock, run self-tests,
edit the tag configuration, start logging, download finished data, and inspect
error messages.

## BitTag Walkthrough

The examples in this section use a classic BitTag so the main screens are
consistent. Other tag types use the same Tag State and Error Log screens; only
the Configuration tab changes by tag family.

### Opening Screen

When Tag Monitor opens with no tag attached, the Tag State tab is visible but
most tag-dependent fields and controls are inactive. Connect a base with an
installed tag, then press **Attach** if the application did not attach
automatically.

![Tag Monitor opening screen](../images/qtmonitor-startup.png)

### Tag State

The Tag State tab has five main areas: Status, Control, Tag Attach, Tag
Information, and Data. The enabled controls depend on the current tag state.

![Tag Monitor idle BitTag state](../images/qtmonitor-main-idle.png)

Check the state, voltage, self-test result, UUID, firmware version, and Git hash
before using a tag in the field. The UUID identifies the physical tag and the
Git hash identifies the firmware build loaded on it.

Synchronize the clock and run self-tests before starting an experiment. Tags
that support calibration, currently CompassTag and IMUTag, also show a
calibration control when attached.

When a tag is running, configuration controls are inactive and **Stop** is
available.

![Tag Monitor running BitTag state](../images/qtmonitor-main-running.png)

When a tag has finished logging, Tag Monitor enables the data controls used to
save tag data.

![Tag Monitor finished BitTag state](../images/qtmonitor-main-finished.png)

### Configuration

The Configuration tab defines the experiment plan that will be written to the
tag. It is one page of panels, and the attached tag's type decides which panels
appear: every tag has a **Logging Schedule** panel, and tags with
user-configurable settings add panels for them, such as the BitTag log format
or the accelerometer. If the page is taller than the screen, it scrolls.

![Tag Monitor classic BitTag configuration](../images/qtmonitor-config-bittag.png)

Pressing **start** writes the displayed configuration to the tag and starts the
tag. Until then, the displayed configuration is only a planned configuration.
Use **save** and **restore** to move configurations through files, and **read**
to reload the current configuration from the attached tag.

### Error Log

The Error Log tab collects application and tag communication messages. If Tag
Monitor behaves unexpectedly, check this tab first and save the log if the
message history will help diagnose the issue.

![Tag Monitor error log](../images/qtmonitor-error-log.png)

## Common Workflow

1. Connect the USB base with the tag installed.
2. Attach to the tag and verify tag type, UUID, firmware, Git hash, and voltage.
3. Synchronize the tag clock.
4. Run self-tests and resolve any reported problem.
5. Configure the schedule and any sensor settings for the experiment.
6. Press **start** to write the configuration and begin logging.
7. After logging finishes, save the tag data before erasing or redeploying.
8. Use the Error Log when attach, configuration, test, or download behavior is
   unexpected.

## Tag Configuration

The Configuration tab is intentionally tag-specific: each tag type shows the
panels for the settings its firmware accepts, and nothing else.

### BitTag

Classic BitTag exposes the full ADXL362 accelerometer configuration. The
schedule controls define the active logging interval and optional hibernation
periods. The BitTag log format controls how activity is binned in memory:
shorter bins provide finer time resolution and consume storage faster.

The **Accelerometer** panel configures ADXL362 range, sample rate, anti-alias
filter, and activity detection thresholds. Most experiments primarily tune the
active and inactive thresholds plus inactivity duration; range, rate, and
filter are usually changed only for specialized deployments.

![Classic BitTag configuration](../images/qtmonitor-config-bittag.png)

### BitTag LE

BitTag LE uses the same schedule and log format as classic BitTag. Its
low-energy firmware fixes the accelerometer's range, rate and filter, so the
**Activity Wakeup** panel shows only the fields users are expected to tune: the
ADXL362 wake/activity thresholds and the inactivity sample count. In this mode,
inactivity is a count at the wake-mode sample rate rather than a duration in
seconds.

![BitTag LE configuration](../images/qtmonitor-config-bittag-le.png)

### BitPresTag

BitPresTag combines BitTag-style activity logging with pressure-tag behavior.
Use the schedule fields for the deployment interval and hibernation windows.
The **Activity Wakeup** panel exposes the wake/activity fields that apply to
the BitPresTag firmware. As with BitTag LE, firmware fixes the other
accelerometer parameters.

![BitPresTag configuration](../images/qtmonitor-config-bitprestag.png)

### PresTag

PresTag shows the schedule with one more setting, the sampling **Period** in
seconds. It has no separate sensor controls.

![PresTag configuration](../images/qtmonitor-config-prestag.png)

### IMUTag

IMUTag's schedule is a **Start Delay** in minutes: logging begins that long
after **start**, and runs until the tag is stopped. The **IMU** panel configures
the sample rate and the accelerometer and gyro ranges. Because IMUTag supports
calibration, the Tag State tab also exposes the calibration control when an
IMUTag is attached.

![IMUTag configuration](../images/qtmonitor-config-imutag.png)

### CompassTag

CompassTag uses the schedule for deployment timing and hibernation windows.
Like IMUTag, it supports calibration from the Tag State tab. It has no separate
sensor controls in Tag Monitor.

![CompassTag configuration](../images/qtmonitor-config-compasstag.png)
