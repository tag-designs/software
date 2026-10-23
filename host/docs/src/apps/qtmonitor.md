# Tag Monitor

Tag Monitor connects to a tag through a USB base and shows the information
needed to prepare, check, run, and recover a data logging experiment. Use it to
verify the tag identity and firmware, synchronize the tag clock, run self-tests,
edit the tag configuration, start logging, download finished data, and inspect
error messages.

## Walkthrough

The main screens below show a CompassTag; the Configuration tab is shown for a
classic BitTag, and for every tag type under [Tag Configuration](#tag-configuration).
Other tag types use the same screens; only the Configuration tab changes by tag
family.

### The window

Commands are in the menus at the top of the window, and on popup menus
(right-click):

| Menu | Commands |
| --- | --- |
| **File** | Save Configuration…, Restore Configuration…, Download Data…, Save Error Log…, Quit |
| **Tag** | Attach, Detach, Read Configuration, Start, Stop, Synchronize Clock, Self-Test, Calibrate (CompassTag and IMUTag), Erase… |
| **Log** | Log Level, Clear Error Log |
| **Help** | Tag Monitor User Guide (opens this page), About Tag Monitor |

A command that does not apply to the tag's current state is greyed out: for
example, Synchronize Clock and Self-Test need an IDLE tag, Stop a running one,
and Download Data a finished one.

The **status strip** below the tabs is visible on every tab. It shows
whether a tag is **Attached** or **Detached**, the tag's type (hover over it for
the board UUID), its state, how far its clock is from this computer's, its
voltage, and its self-test result. **Start** and **Stop** are on the right of
the strip.

### Opening screen

When Tag Monitor opens with no tag attached, the strip shows **Detached** and
most commands are greyed out. Connect a base with an installed tag, then choose
**Tag ▸ Attach** if the application did not attach automatically.

![Tag Monitor opening screen](../images/qtmonitor-startup.png)

### Tag

The Tag tab shows the tag's identity and how much data it holds. Right-click it
for the tag commands.

![Tag Monitor with an idle CompassTag](../images/qtmonitor-main-idle.png)

Check the state, voltage, self-test result, UUID, firmware version, and Git hash
before using a tag in the field. The UUID identifies the physical tag and the
Git hash identifies the firmware build loaded on it.

Choose **Tag ▸ Synchronize Clock** and **Tag ▸ Self-Test** before starting an
experiment; the strip shows the clock offset and the self-test result. The
self-tests usually finish in well under a second, so the tag's state rarely
shows TEST; the strip shows **Running** until the result arrives, and the Error
Log records each run's start and result with the tag's UUID. Tags that support
calibration, currently CompassTag and IMUTag, also have **Tag ▸ Calibrate**.

When a tag is running, the configuration cannot be edited and **Stop** is
available.

![Tag Monitor with a running CompassTag](../images/qtmonitor-main-running.png)

When a tag has finished logging, **File ▸ Download Data…** saves its data.

![Tag Monitor with a finished CompassTag](../images/qtmonitor-main-finished.png)

### Configuration

The Configuration tab defines the experiment plan that will be written to the
tag. It is one page of panels, and the attached tag's type decides which panels
appear: every tag has a **Logging Schedule** panel, and tags with
user-configurable settings add panels for them, such as the BitTag log format
or the accelerometer. If the page is taller than the screen, it scrolls.

![Tag Monitor classic BitTag configuration](../images/qtmonitor-config-bittag.png)

**Start** writes the displayed configuration to the tag and starts it. Until
then, the displayed configuration is only a planned configuration. Use
**File ▸ Save Configuration…** and **File ▸ Restore Configuration…** to move
configurations through files (see [Configuration Files](../reference/config-files.md)),
and **Tag ▸ Read Configuration** to reload the attached tag's own. These are
also on the page's popup menu.

### Error Log

The Error Log tab collects application and tag communication messages. If Tag
Monitor behaves unexpectedly, check this tab first, and save the log
(**File ▸ Save Error Log…**) if the message history will help diagnose the
issue. **Log ▸ Log Level** sets how much detail is recorded. Right-click the
log to copy, save or clear it.

![Tag Monitor error log](../images/qtmonitor-error-log.png)

## Common Workflow

1. Connect the USB base with the tag installed.
2. Attach to the tag and verify tag type, UUID, firmware, Git hash, and voltage.
3. Synchronize the tag clock.
4. Run the self-tests and resolve any reported problem.
5. Configure the schedule and any sensor settings for the experiment.
6. Press **Start** to write the configuration and begin logging.
7. After logging finishes, download the tag's data before erasing or
   redeploying.
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
calibration, **Tag ▸ Calibrate** is available when an IMUTag is attached.

![IMUTag configuration](../images/qtmonitor-config-imutag.png)

### CompassTag

CompassTag uses the schedule for deployment timing and hibernation windows.
Like IMUTag, it supports calibration (**Tag ▸ Calibrate**). It has no separate
sensor controls in Tag Monitor.

![CompassTag configuration](../images/qtmonitor-config-compasstag.png)
