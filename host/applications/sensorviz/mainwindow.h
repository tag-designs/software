#ifndef SENSORVIZ_MAINWINDOW_H
#define SENSORVIZ_MAINWINDOW_H

#include <QAction>
#include <QColor>
#include <QLabel>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QPrinter>
#include <QSet>
#include <QSharedPointer>
#include <QTabWidget>
#include <QTextEdit>
#include <QVector>

#include <qcustomplot.h>

#include "compass_display.h"
#include "compass_types.h"
#include "sensorstream.h"

class QQuickWidget;
class QSplitter;

// MainWindow is the coordination object for sensorViz. It owns the loaded
// SensorLog, the stream list currently available to the user, the menu actions
// that select those streams, and the QCustomPlot state used to draw them.
//
// The code is split across several .cpp files to keep bug hunting manageable:
//
// - mainwindow.cpp builds static UI objects: menus, tabs, plot widget, file-info
//   pane, status bar, and cursor items. If a widget is missing, a signal is not
//   connected, or a menu action needs to exist at startup, start there.
//
// - dataloading.cpp handles the "File -> Load" workflow. It asks the SQLite
//   loader for a SensorLog, replaces the current stream list, creates the View
//   menu actions for each stream, updates the File Info tab, and refreshes the
//   plot. If a file opens but the app state is wrong after loading, start there.
//
// - sqlite_loader.cpp reads tagcore's streams metadata table from the on-disk
//   SQLite schema. It maps scalar stream rows into SensorStream objects and
//   grouped record-column rows into SensorRecordSet objects. If a table exists
//   but does not load correctly, start there.
//
// - stream_actions.cpp owns stream visibility and range menu actions.
//
// - transforms.cpp owns display-only derived streams such as altitude and
//   activity low-pass.
//
// - plotting.cpp rebuilds QCustomPlot graphs and dynamic axes.
//
// - interaction.cpp owns cursors, print preview, UTC offset, context menus, and
//   mouse readout.
//
// - controls.cpp contains small shared display helpers and general actions.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void loadLog();
    void showAbout();
    void resetZoom();
    void setUtcOffset();
    void altitudeToggled(bool checked);
    void activityFilterToggled(bool checked);
    void compassDerivedToggled(bool checked);
    void setDeclination();
    void batteryForwardToggled(bool checked);
    void showCalibrationConstants();
    void setStreamRangeFromAction();
    void setStreamColorFromAction();
    void printPlot();
    void renderPlot(QPrinter *printer);
    void zoomToCursors();
    void plotDoubleClick(QMouseEvent *event);
    void showPlotContextMenu(const QPoint &pos);
    void showMousePosition(QMouseEvent *event);

private:
    // UI construction. These run once from the constructor before any log is
    // loaded. Later code updates visibility and checked state, but does not
    // recreate these static menus or widgets.
    void createUi();
    void createActions();
    QString unitsSuffix(const SensorStream &stream) const;
    double pressureToAltitude(double pressure_mbar) const;

    // Log metadata and tag-dependent menu state. updateTransformActions() is
    // called whenever streams are added/removed because transform availability
    // is inferred from actual stream ids rather than tag type strings.
    void updateMetadata();
    void updateTransformActions();

    // Streams are the single source of truth for both raw and derived data.
    // Raw streams have checkable QAction objects in stream_actions_; derived
    // streams are enabled by transform actions and intentionally do not get
    // duplicated in the View menu. If a stream exists but is not visible, first
    // check streams_, then the matching QAction or transform action.
    void addOrReplaceStream(const SensorStream &stream, bool checked);
    void removeStream(const QString &id);
    void clearStreamActions();
    void addStreamAction(const SensorStream &stream, bool checked);
    QAction *streamActionById(const QString &id) const;
    bool hasStream(const QString &id) const;
    const SensorStream *streamById(const QString &id) const;
    const SensorRecordSet *recordSetById(const QString &id) const;
    QVector<SensorStream> visibleStreams() const;

    // Range actions are rebuilt from visibleStreams() whenever stream
    // visibility or derived-stream availability changes. Custom ranges are
    // keyed by stable stream id, so adding a new stream normally requires no
    // new range UI code.
    void clearRangeActions();
    void rebuildRangeActions();
    void addRangeAction(const SensorStream &stream);
    void setStreamRange(const QString &id);
    QCPRange defaultRangeForStream(const SensorStream &stream) const;

    // Color actions are viewer preferences. The SQLite log supplies default
    // colors through sqlite_loader.cpp; users can override them for the current
    // session without changing stream metadata or saved logs.
    QColor effectiveStreamColor(const SensorStream &stream) const;
    void clearColorActions();
    void rebuildColorActions();
    void addColorAction(const SensorStream &stream);
    void setStreamColor(const QString &id);
    void updateCompassDisplay(double epoch);

    // Plot layout and cursor helpers. refreshPlot() rebuilds graphs and axes
    // from current actions while preserving the visible time window;
    // refreshPlotFullRange() is used after a new file is loaded or after Reset
    // Zoom clears custom y-axis ranges.
    void refreshPlot();
    void refreshPlotFullRange();
    void rebuildPlot(bool reset_x_range);
    void clearDynamicAxes();
    QCPAxis *axisForStream(
        SensorAxisSide side,
        int side_index,
        const SensorStream &stream);
    QString streamValueAt(const SensorStream &stream, double epoch) const;
    void resetCursorsToView();

    SensorLog log_;
    QVector<SensorStream> streams_;
    QString current_path_;
    int utc_offset_ = 0;
    double sea_level_pressure_ = 1013.25;
    double activity_low_pass_seconds_ = 600.0;
    double declination_degrees_ = 0.0;
    bool battery_forward_ = true;
    QVector<CompassDerivedSample> compass_samples_;

    // User-set y-axis ranges. custom_axis_ranges_ may also contain a linked
    // altitude range derived from pressure; explicit_axis_ranges_ records only
    // ranges the user set directly so pressure can stop driving altitude once
    // altitude has its own manual range.
    QMap<QString, QCPRange> custom_axis_ranges_;
    QSet<QString> explicit_axis_ranges_;
    QMap<QString, QColor> custom_stream_colors_;

    QWidget *central_ = nullptr;
    QCustomPlot *plot_ = nullptr;
    QSplitter *plot_splitter_ = nullptr;
    QQuickWidget *compass_widget_ = nullptr;
    CompassDisplay compass_display_;
    QTextEdit *info_ = nullptr;
    QLabel *status_ = nullptr;
    QTabWidget *tabs_ = nullptr;

    QAction *load_action_ = nullptr;
    QAction *about_action_ = nullptr;
    QAction *reset_action_ = nullptr;
    QAction *utc_offset_action_ = nullptr;
    QAction *altitude_action_ = nullptr;
    QAction *activity_filter_action_ = nullptr;
    QAction *compass_derived_action_ = nullptr;
    QAction *declination_action_ = nullptr;
    QAction *battery_forward_action_ = nullptr;
    QAction *calibration_constants_action_ = nullptr;
    QAction *print_action_ = nullptr;
    QAction *zoom_to_cursors_action_ = nullptr;
    QAction *view_stream_separator_ = nullptr;
    QAction *configuration_transform_separator_ = nullptr;
    QMenu *view_menu_ = nullptr;
    QMenu *visible_streams_menu_ = nullptr;
    QMenu *configuration_menu_ = nullptr;
    QMenu *range_menu_ = nullptr;
    QMenu *color_menu_ = nullptr;
    QVector<QAction *> stream_actions_;
    QVector<QAction *> range_actions_;
    QVector<QAction *> color_actions_;

    QSharedPointer<QCPAxisTickerDateTime> date_ticker_;
    QVector<QCPAxis *> dynamic_axes_;
    QCPItemLine *left_cursor_ = nullptr;
    QCPItemLine *right_cursor_ = nullptr;
};

#endif
