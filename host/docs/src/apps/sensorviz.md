# SensorViz

Use SensorViz to inspect SQLite sensor logs from pressure and activity tags.
The application is intended for comparing raw sensor streams and derived views,
such as altitude computed from pressure or filtered activity.

## Open a Data File

1. Open SensorViz.
2. Choose **File > Open**.
3. Select a downloaded SQLite log file.
4. Review the loaded tag and file details in the **File Info** tab.
5. Return to the plot tab to inspect the available sensor streams.

## Choose Streams

Describe how the **View** menu and plot context menu are used to show or hide
available streams. Note which streams are visible by default and which streams
are only available for specific tag types.

## Configure Derived Views

Add notes for transform-specific controls here:

- Altitude from pressure, including sea-level pressure.
- Activity low-pass filtering, including the time constant.
- Any future derived streams that are added to SensorViz.

## Adjust Plot Ranges

Describe how to change pressure and altitude ranges, reset zoom, and use cursor
selection to zoom into a time interval.

## Print or Export

Describe print preview, printing, and any supported export workflow here.

## Troubleshooting

Add common issues here, such as unsupported file types, missing streams, or a
file that loads but has no plottable data.

## Screenshot Placeholders

```md
![SensorViz plot view](../images/sensorviz-plot-view.png)
![SensorViz file info view](../images/sensorviz-file-info.png)
```
