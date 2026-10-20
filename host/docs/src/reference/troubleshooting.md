# Troubleshooting

Use this page for problems shared across multiple applications.

## Device Not Detected

- Confirm the device is connected.
- Try a different USB cable or port.
- Check platform-specific driver or permission requirements.

## Application Does Not Start

- Confirm the package was fully extracted or installed.
- On Windows, launch the top-level executable from the package folder.
- On macOS, check whether the app was blocked by system security settings.
- On macOS, a command-line tool such as `tag-info` cannot be started by
  double-clicking it in the Finder, which may report it as unusable. Run it
  from Terminal instead; see [Command-Line Tools](../cli/index.md).

## Data Looks Wrong

- Confirm the correct tag model and firmware version.
- Check that calibration was performed when required.
- Verify the data file was downloaded completely.

