#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDateTime>
#include <QList>
#include <QTimer>
#include <QVector>
#include <QVector3D>
#include "tagclass.h"
#include "ui_mainwindow.h"

#include "attitude_display.h"
#include "compass_display.h"
#include "tag.pb.h"
#include "compassdata.h"



class QAction;
class QMenu;

extern int log_level;

/**
 * @brief Startup-time switches for live, replay, and documentation modes.
 *
 * @details The normal interactive application uses the default values and
 *          attempts to attach to a USB tag base. Documentation automation
 *          populates this struct from command-line options so the same
 *          MainWindow can be driven from a saved calibration capture without
 *          real hardware. Replay mode intentionally exercises the production
 *          calibration/orientation UI paths instead of a screenshot-only mock.
 */
struct MainWindowOptions {
  /**
   * @brief Path to a saved calibration capture JSON file used as fake tag data.
   */
  QString replayCapturePath;

  /**
   * @brief Optional replay collection milestone to preload before showing UI.
   *
   * @details Values are clamped by main.cpp to 0..100. The default -1 leaves
   *          the replay capture loaded but does not feed any samples.
   */
  int replayPercent = -1;

  /**
   * @brief Generate the standard calibration milestone screenshot set and exit.
   */
  bool captureReplayScreenshots = false;

  /**
   * @brief Generate the disconnected startup screenshot and exit.
   */
  bool captureStartupScreenshot = false;

  /**
   * @brief Generate deterministic orientation screenshots and exit.
   */
  bool captureOrientationScreenshot = false;

  /**
   * @brief Suppress USB probing when opening a documentation-only window.
   */
  bool skipAutoAttach = false;

  /**
   * @brief Optional fixed pose for orientation screenshots.
   *
   * @details Expected order is heading, pitch, roll, dip angle, magnetic field,
   *          and gravity. If the list is absent or malformed, MainWindow uses a
   *          built-in pose chosen to make the forward/backward convention
   *          difference visible in the documentation images.
   */
  QList<float> orientationPose;

  /**
   * @brief Output directory for generated screenshots.
   */
  QString screenshotDir;

  /**
   * @brief Filename prefix for calibration milestone screenshots.
   */
  QString screenshotPrefix = "qtcalibrate-collection";
};

/**
 * @brief Main window for live calibration and documentation replay capture.
 *
 * @details MainWindow owns the USB/tag workflow, calibration controls, log
 *          display, and the embedded shared sensorui QML widgets used to
 *          preview compass and attitude. The documentation hooks reuse those
 *          same controls with saved sample data so generated screenshots show
 *          the real application state rather than a separate fixture UI.
 */
class MainWindow : public QMainWindow {
  Q_OBJECT

public:

  explicit MainWindow(const MainWindowOptions &options = MainWindowOptions(),
                      QWidget *parent = nullptr);
  ~MainWindow();
  bool shouldQuitAfterStartup() const;

public slots:

  /**
   * @brief Capture 0%, 25%, 50%, and 100% calibration replay screenshots.
   *
   * @details Requires a loaded replay capture. This slot is scheduled from
   *          main.cpp after the window has been shown so native window-frame
   *          screenshots include the same chrome seen by users.
   */
  void captureReplayScreenshots();

  /**
   * @brief Capture the application immediately after startup with no replay.
   */
  void captureStartupScreenshot();

  /**
   * @brief Capture deterministic battery-forward and battery-backward views.
   *
   * @details The orientation screenshots first run the replay through a full
   *          calibration so the calibration parameters are valid, then inject a
   *          fixed pose. This keeps the images stable while still documenting
   *          the real orientation tab.
   */
  void captureOrientationScreenshot();
 

signals:

#ifndef __arm64__
  void lBar(int currProgress, int total);
  void DisplayMessage(int msgType, QString str);
  void logMessage(int msgType, QString str);
#endif

  void SectorsErased(int);
  void IdleState(void);


private slots:

  // attach/detach to tag

  bool Attach();
  void Detach();

  // timer tick

  void TriggerUpdate();
  void TriggerQualityUpdate();

  // control tab events

  void on_attachButton_clicked();
  void on_detachButton_clicked();
  void on_streamCheckBox_toggled(bool);

  // calibration 


  void on_startButton_clicked();
  void on_stopButton_clicked();
  void on_clearButton_clicked();
  void calibration_update(void);
  void on_saveButton_clicked();
  void on_loadButton_clicked();

  // log and orientation display actions

  void on_logsaveButton_clicked();
  void on_logclearButton_clicked();
  void on_loglevelBox_currentIndexChanged(int index);
  void on_actionSave_Sample_Capture_triggered();
  void setDeclination();
  void batteryForwardToggled(bool checked);
  void showOrientationContextMenu(const QPoint &pos);



private:

  void logWindowInit(void);
  void scatterGraphInit(void);
  void orientationControlsInit(void);
  void updateDeclinationActionText();
  void resetCalibrationDisplay();
  /**
   * @brief Load saved calibration samples for fake-tag replay.
   *
   * @details The expected schema is written by saveSampleCapture(). Invalid or
   *          incomplete sample entries are skipped so older captures can still
   *          be useful if they contain magnetometer data.
   */
  bool loadReplayCapture(const QString &path);

  /**
   * @brief Put the UI in an attached-looking state backed by replay samples.
   */
  void setupReplayTag();

  /**
   * @brief Feed replay data into the calibration view up to a milestone.
   *
   * @param[in] percent Collection percentage, clamped to 0..100.
   */
  void prepareReplayMilestone(int percent);

  /**
   * @brief Prepare the orientation tab for screenshot capture.
   *
   * @param[in] batteryForwardConvention true for the battery-forward display
   *                                     convention, false for battery-backward.
   */
  void prepareReplayOrientation(bool batteryForwardConvention);

  /**
   * @brief Inject the fixed documentation pose into the orientation widgets.
   *
   * @details This bypasses the replay sample's instantaneous accelerometer
   *          attitude so forward/backward documentation images differ only by
   *          the selected battery convention, not by random captured motion.
   */
  void applyDocumentationOrientationPose(bool batteryForwardConvention);

  /**
   * @brief Reset all replay-driven calibration UI state before a milestone.
   */
  void resetReplayCollection();

  /**
   * @brief Save a native-frame screenshot of the current MainWindow.
   *
   * @details Uses QScreen::grabWindow over frameGeometry() first because a
   *          plain QWidget grab omits window frame pixels on macOS and clips
   *          the documentation screenshots.
   */
  bool saveCurrentScreenshot(const QString &path);

  /**
   * @brief Feed one live or replay sample through orientation and calibration.
   */
  void processCalibrationSample(const QVector3D &mag, bool hasAccel,
                                const QVector3D &accel, int batchIndex,
                                int sampleIndex);

  /**
   * @brief Return the next fake-tag sample from the loaded capture.
   */
  bool nextReplaySample(QVector3D &mag, bool &hasAccel, QVector3D &accel,
                        int &batchIndex, int &sampleIndex);

  /**
   * @brief Start collecting samples for a maintainable replay fixture.
   */
  void beginSampleCapture();

  /**
   * @brief Append a sample to the current capture if capture is active.
   */
  void recordCaptureSample(int batchIndex, int sampleIndex, const QVector3D &mag,
                           bool hasAccel, const QVector3D &accel);

  /**
   * @brief Finish the current sample capture and enable saving if data exists.
   */
  void finishSampleCapture();

  /**
   * @brief Drop any in-memory capture and disable the save action.
   */
  void clearSampleCapture();

  /**
   * @brief Write the current capture as JSON for future replay runs.
   */
  bool saveSampleCapture(const QString &path);
  // These methods bridge live orientation results into the shared sensorui
  // facades. They intentionally avoid direct QML method calls in MainWindow.
  void rotateImage(QQuaternion qt);
  void setOrientation(float h, float p, float r, float d, float f, float g);

  // CompassData owns the inherited calibration solver state. MainWindow owns
  // the tag connection and decides when to feed samples into that state.
  CompassData magnetic;
  CompassDisplay compassDisplay;
  AttitudeDisplay attitudeDisplay;
  Tag tag;
  Config config;
  TagInfo info;
  Ui::MainWindow ui;
  QTimer timer;
  QTimer qualitytimer;
  UsbDev usbdev;
  QMenu *configurationMenu = nullptr;
  QAction *declinationAction = nullptr;
  QAction *batteryForwardAction = nullptr;
  double declinationDegrees = 0.0;
  bool batteryForward = true;

  bool isCalibrating = false;
  bool isOrienting = false;
  bool isStreaming = false;

  struct ReplaySample {
    QVector3D mag;
    bool hasAccel = false;
    QVector3D accel;
  };

  QVector<ReplaySample> replaySamples;
  QString replayCapturePath;
  QString screenshotDir;
  QString screenshotPrefix;
  bool replayEnabled = false;
  bool captureReplayScreenshotsOnStartup = false;
  bool captureStartupScreenshotOnStartup = false;
  bool captureOrientationScreenshotOnStartup = false;
  bool quitAfterStartup = false;
  int replayCursor = 0;
  QList<float> orientationPose;

  struct CalibrationCaptureSample {
    int batchIndex = 0;
    int sampleIndex = 0;
    QVector3D mag;
    bool hasAccel = false;
    QVector3D accel;
  };

  QVector<CalibrationCaptureSample> captureSamples;
  QDateTime captureStartedUtc;
  QDateTime captureStoppedUtc;
  int captureBatchIndex = 0;
  bool captureActive = false;
  bool captureReady = false;


};

#endif // MAINWINDOW_H
