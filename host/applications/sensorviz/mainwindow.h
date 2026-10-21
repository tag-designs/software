#ifndef SENSORVIZ_MAINWINDOW_H
#define SENSORVIZ_MAINWINDOW_H

#include <QAction>
#include <QColor>
#include <QLabel>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QPoint>
#include <QPointF>
#include <QPrinter>
#include <QSet>
#include <QSharedPointer>
#include <QTabWidget>
#include <QTextEdit>
#include <QVector>

#include <qcustomplot.h>

#include "compass_display.h"
#include "compass_types.h"
#include "plot_spec.h"
#include "plot_view.h"
#include "sensor_preferences.h"
#include "sensorstream.h"

class QQuickWidget;
class QSplitter;

/**
 * @struct  MainWindowOptions
 * @brief   Startup options used by maintainer automation.
 *
 * @details The normal interactive application passes the default-constructed
 *          value. Documentation capture runs pass fixture paths and capture
 *          names so SensorViz can load real data and grab deterministic
 *          screenshots without opening modal file dialogs.
 */
struct MainWindowOptions
{
    QString loadLogPath;          ///< SQLite log loaded after the window is shown.
    QString loadPreferencesPath;  ///< SensorViz preference JSON loaded for the fixture log.
    QString captureScreenshot;    ///< Single capture name; may be empty when a suite is used.
    QString captureSuite;         ///< Capture suite name such as @c startup, @c menus, or @c all.
    QString screenshotDir;        ///< Destination directory for generated PNG files.
    bool noUserPrompts = false;   ///< Report fixture/capture errors to stderr instead of dialogs.
};

// MainWindow is the Qt adapter over sensorViz's Qt-free core. It owns the
// loaded SensorLog, the ViewModel holding the view state (streams, visibility,
// ranges, colors, sides, title and settings), the menu actions that show and
// change that state, and the PlotView that draws the PlotSpec built from it.
//
// The code is split across several .cpp files to keep bug hunting manageable:
//
// - mainwindow.cpp builds static UI objects: menus, tabs, plot widget, file-info
//   pane, status bar, and cursor items. If a widget is missing, a signal is not
//   connected, or a menu action needs to exist at startup, start there.
//
// - dataloading.cpp handles the "File -> Load" workflow. It asks the SQLite
//   loader for a SensorLog, resets the ViewModel for its streams, updates the
//   File Info tab, and refreshes the plot. If a file opens but the app state is wrong after loading, start there.
//
// - sqlite_loader.cpp reads tagcore's streams metadata table from the on-disk
//   SQLite schema. It maps scalar stream rows into SensorStream objects and
//   grouped record-column rows into SensorRecordSet objects. If a table exists
//   but does not load correctly, start there.
//
// - sensor_preferences.cpp owns the per-tag preference map and its JSON
//   load/store; the ViewModel applies and captures them.
//
// - stream_actions.cpp owns the stream and range actions (views of the
//   ViewModel) and the visibility, side, color and range dialogs.
//
// - transforms.cpp owns display-only derived streams such as altitude and
//   activity low-pass.
//
// - plotting.cpp hands PlotView (plot_view.*), which owns everything drawn
//   with QCustomPlot, the PlotSpec the ViewModel builds. View state and
//   display policy are in the Qt-free core (core/view_model.*,
//   core/plot_spec.*).
//
// - interaction.cpp owns cursor and print commands, UTC offset, context
//   menus, and the mouse readout.
//
// - controls.cpp contains small shared display helpers and general actions.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(
        const MainWindowOptions &options = MainWindowOptions(),
        QWidget *parent = nullptr);

private slots:
    void loadLog();
    void loadPreferences();
    void savePreferences();
    void loadDefaultPreferences();
    void showAbout();
    void showUserGuide();
    void editGraphTitle();
    void setGraphTitleVisible(bool visible);
    void resetZoom();
    void setUtcOffset();
    void setSeaLevelPressure();
    void activityFilterToggled(bool checked);
    void setDeclination();
    void batteryForwardToggled(bool checked);
    void showCalibrationConstants();
    void showVisibleStreamsDialog();
    void showAxisSidesDialog();
    void showStreamColorsDialog();
    void setStreamRangeFromAction();
    void printPlot();
    void renderPlot(QPrinter *printer);
    void zoomToCursors();
    void setCursorsVisible(bool visible);
    void showPlotContextMenu(const QPoint &global_pos);
    void showMousePosition(double time);

private:
    // UI construction. These run once from the constructor before any log is
    // loaded. Later code updates visibility and checked state, but does not
    // recreate these static menus or widgets.
    void createUi();
    void createActions();
    QString unitsSuffix(const SensorStream &stream) const;
    double pressureToAltitude(double pressure_mbar) const;
    double pressureToAltitude(double pressure_mbar, double temperature_c) const;

    // Log metadata and tag-dependent menu state. updateLoadedStateActions()
    // keeps startup discoverable but disabled except for Load/About.
    // updateTransformActions() is called whenever streams are added/removed
    // because transform availability is inferred from actual stream ids rather
    // than tag type strings.
    void updateLoadedStateActions();
    void updateMetadata();
    void updateTransformActions();
    void setLogLoadInProgress(bool loading, const QString &path = {});
    void applyLoadedLog(const QString &path, const SensorLog &log);
    void createAltitudeStream();
    void createImuMagnitudeStreams();
    // Compass views are built from orientation quaternions: stored ones from a
    // processed log's orientation record set when present, otherwise derived
    // from compass_raw and the calibration constants.
    const SensorRecordSet *storedOrientationRecordSet() const;
    bool hasCompassOrientationSource() const;
    bool loadCompassSamples(QString &error);
    void addCompassStream(SensorStream stream, const QVector<double> &values, bool checked);
    void createCompassPlotStreams();
    QString preferenceKey() const;
    void rememberCurrentPreferences();
    void applyPreferencesForCurrentTag();
    bool loadPreferencesFromFile(const QString &path, QString &error);
    bool savePreferencesToFile(const QString &path, QString &error);

    /**
     * @brief   Loads a SQLite log for command-line documentation automation.
     *
     * @details Runs the same SQLite conversion and state application used by
     *          File > Load, but synchronously so screenshot capture can wait
     *          for menus, transforms, and plots to settle.
     *
     * @param[in] path   SQLite database path to load.
     * @param[out] error Human-readable failure reason when loading fails.
     *
     * @return  @c true when the log was loaded and applied, @c false on a
     *          loader or file error.
     */
    bool loadLogFromPathForDocumentation(const QString &path, QString &error);

    /**
     * @brief   Applies documentation startup options after the window is shown.
     *
     * @details This is the only entry point for command-line documentation
     *          hooks. It keeps fixture loading and screenshot capture out of
     *          the normal interactive startup path.
     */
    void runDocumentationStartup();

    /**
     * @brief   Saves a named documentation screenshot.
     *
     * @param[in] name Capture name or PNG filename.
     *
     * @return  @c true when the PNG was written successfully.
     */
    bool captureDocumentationScreenshot(const QString &name);

    /**
     * @brief   Saves a deterministic suite of documentation screenshots.
     *
     * @param[in] suite Capture suite name such as @c startup, @c menus, or
     *                  @c all.
     *
     * @return  @c true when every capture in the suite succeeds.
     */
    bool captureDocumentationSuite(const QString &suite);

    /**
     * @brief   Emits capture failures through the automation-friendly channel.
     *
     * @param[in] message Failure text suitable for stderr or a message box.
     */
    void reportDocumentationError(const QString &message);

    // The view state lives in view_ (sensorviz::ViewModel, Qt-free): the
    // streams and which are shown, custom ranges, colors and sides, title,
    // UTC offset, cursors, gap mode and transform parameters. Menus and
    // dialogs are views of it. syncActions() sets every QAction's checked
    // state, text and visibility from it; toggles and dialogs call its
    // setters. stream_actions_ mirror the non-derived streams' visibility for
    // the Visible Streams dialog. Single-output transforms such as altitude
    // and low-pass activity are enabled by transform actions.
    void addOrReplaceStream(const SensorStream &stream, bool checked);
    void removeStream(const QString &id);
    void syncActions();
    void syncStreamActions();
    void streamVisibilityChanged();
    bool isStreamVisible(const QString &id) const;
    void applyStreamVisibility(const QSet<QString> &visible_ids);
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

    // Axis side choices are viewer preferences layered over stream defaults.
    // The metadata/default remains in SensorStream::axisSide; this override is
    // useful when a particular data combination reads better with a different
    // left/right arrangement.
    SensorAxisSide effectiveAxisSide(const SensorStream &stream) const;
    void updateAxisSidesAction();

    // Color actions are viewer preferences. sensor_preferences.cpp supplies
    // default colors; users can override them for the current session without
    // changing stream metadata or saved logs.
    QColor effectiveStreamColor(const SensorStream &stream) const;
    void updateColorsAction();
    void updateCompassDisplay(double epoch);
    QMenu *createPlotContextMenu(QWidget *parent);
    QString formatPlotTime(double time_seconds) const;
    void updateTimeAxisForLog();

    // The plot. view_.plotSpec() builds the PlotSpec (core/plot_spec.h) from
    // the view state and plotLogInfo(), and PlotView draws it. refreshPlot()
    // keeps the visible time window; refreshPlotFullRange() is used after a
    // new file is loaded or after Reset Zoom clears custom y-axis ranges.
    void refreshPlot();
    void refreshPlotFullRange();
    void rebuildPlot(bool reset_x_range);
    sensorviz::PlotLogInfo plotLogInfo() const;
    QString streamValueAt(const SensorStream &stream, double epoch) const;

    // Plot data in log_.data: the hover readout and value ranges read it,
    // transforms read their inputs from it and register their outputs in it.
    const sensordata::StreamInfo *plotDataInfo(const SensorStream &stream) const;
    sensordata::StreamInfo derivedPlotInfo(const SensorStream &stream) const;
    bool readStreamDoubles(
        const SensorStream &stream,
        std::vector<double> &time,
        std::vector<double> &value) const;
    void releasePlotData(const SensorStream &stream);

    SensorLog log_;
    sensorviz::ViewModel view_;
    QString current_path_;
    bool log_load_in_progress_ = false;
    MainWindowOptions options_;

    QVector<CompassDerivedSample> compass_samples_;
    // Where compass_samples_ came from, for the File Info pane, and a stored
    // stream on that record set's time axis, which the compass streams share.
    QString compass_source_;
    sensordata::StreamId compass_time_source_ = sensordata::kNoStream;

    // Preferences are keyed by tag type/profile and store only differences from
    // sensorViz display defaults. The currently loaded tag's are applied to
    // and captured from view_.
    QMap<QString, SensorVizPreferences> preferences_by_tag_type_;
    bool suppress_preference_updates_ = false;

    QWidget *central_ = nullptr;
    PlotView *plot_view_ = nullptr;
    QSplitter *plot_splitter_ = nullptr;
    QQuickWidget *compass_widget_ = nullptr;
    CompassDisplay compass_display_;
    QTextEdit *info_ = nullptr;
    QLabel *status_ = nullptr;
    QTabWidget *tabs_ = nullptr;

    QAction *load_action_ = nullptr;
    QAction *load_preferences_action_ = nullptr;
    QAction *save_preferences_action_ = nullptr;
    QAction *default_preferences_action_ = nullptr;
    QAction *about_action_ = nullptr;
    QAction *user_guide_action_ = nullptr;
    QAction *edit_title_action_ = nullptr;
    QAction *show_title_action_ = nullptr;
    QAction *reset_action_ = nullptr;
    QAction *utc_offset_action_ = nullptr;
    QAction *sea_level_pressure_action_ = nullptr;
    QAction *activity_filter_action_ = nullptr;
    QAction *declination_action_ = nullptr;
    QAction *battery_forward_action_ = nullptr;
    QAction *calibration_constants_action_ = nullptr;
    QAction *print_action_ = nullptr;
    QAction *zoom_to_cursors_action_ = nullptr;
    QAction *show_cursors_action_ = nullptr;
    QAction *break_gaps_action_ = nullptr;
    QAction *visible_streams_action_ = nullptr;
    QAction *axis_sides_action_ = nullptr;
    QAction *colors_action_ = nullptr;
    QAction *view_stream_separator_ = nullptr;
    QAction *configuration_transform_separator_ = nullptr;
    QMenu *view_menu_ = nullptr;
    QMenu *configuration_menu_ = nullptr;
    QMenu *range_menu_ = nullptr;
    QMenu *file_menu_ = nullptr;
    QMenu *preferences_menu_ = nullptr;
    QMenu *help_menu_ = nullptr;
    QVector<QAction *> stream_actions_;
    QVector<QAction *> range_actions_;

    // Resolution of the printer printPlot() set up, before renderPlot()
    // switches it to screen coordinates; 0 when unknown.
    int print_device_dpi_ = 0;
};

#endif
