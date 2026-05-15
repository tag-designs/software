#ifndef SENSORVIZ_MAINWINDOW_H
#define SENSORVIZ_MAINWINDOW_H

#include <QAction>
#include <QLabel>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QPrinter>
#include <QSharedPointer>
#include <QTabWidget>
#include <QTextEdit>
#include <QVector>

#include <qcustomplot.h>

#include "sensorstream.h"

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
// - sensorprofile.cpp describes known log tables, default stream visibility,
//   future record sets, and transform metadata. If a new tag type or table needs
//   to be recognized, start there.
//
// - sqlite_loader.cpp applies the profile definitions to the on-disk SQLite
//   schema. It maps scalar tables into SensorStream objects and multi-column
//   tables into SensorRecordSet objects. If a table definition exists but does
//   not load correctly, start there.
//
// - controls.cpp contains runtime behavior after a file is loaded: stream
//   bookkeeping, pressure/activity transforms, tag-dependent menu visibility,
//   plotting, axes, cursors, mouse readout, UTC offset, and printing. If a menu
//   item does the wrong thing or the plot looks wrong, start there.
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
    void setPressureRange();
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

    // Log metadata and tag-dependent menu state. updateTransformActions() is
    // called whenever streams are added/removed because transform availability
    // is inferred from actual stream ids rather than tag type strings.
    void updateMetadata();
    void updateTransformActions();

    // Streams are the single source of truth for both raw and derived data.
    // QAction checked state determines which streams are currently plotted.
    // This means a stream bug may live in two places: the SensorStream data in
    // streams_, or the matching QAction in stream_actions_.
    void addOrReplaceStream(const SensorStream &stream, bool checked);
    void removeStream(const QString &id);
    void clearStreamActions();
    void addStreamAction(const SensorStream &stream, bool checked);
    QAction *streamActionById(const QString &id) const;
    bool hasStream(const QString &id) const;
    const SensorStream *streamById(const QString &id) const;
    QVector<SensorStream> visibleStreams() const;

    // Plot layout and cursor helpers. refreshPlot() rebuilds from current
    // actions while preserving the visible time window; refreshPlotFullRange()
    // is used after a new file is loaded or the user explicitly resets zoom.
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
    QMap<QString, QCPRange> custom_axis_ranges_;

    QWidget *central_ = nullptr;
    QCustomPlot *plot_ = nullptr;
    QTextEdit *info_ = nullptr;
    QLabel *status_ = nullptr;
    QTabWidget *tabs_ = nullptr;

    QAction *load_action_ = nullptr;
    QAction *about_action_ = nullptr;
    QAction *reset_action_ = nullptr;
    QAction *utc_offset_action_ = nullptr;
    QAction *altitude_action_ = nullptr;
    QAction *activity_filter_action_ = nullptr;
    QAction *pressure_range_action_ = nullptr;
    QAction *print_action_ = nullptr;
    QAction *zoom_to_cursors_action_ = nullptr;
    QAction *view_stream_separator_ = nullptr;
    QAction *configuration_sensor_separator_ = nullptr;
    QAction *configuration_transform_separator_ = nullptr;
    QMenu *view_menu_ = nullptr;
    QMenu *configuration_menu_ = nullptr;
    QVector<QAction *> stream_actions_;

    QSharedPointer<QCPAxisTickerDateTime> date_ticker_;
    QVector<QCPAxis *> dynamic_axes_;
    QCPItemLine *left_cursor_ = nullptr;
    QCPItemLine *right_cursor_ = nullptr;
};

#endif
