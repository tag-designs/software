/**
 * @file    mainwindow.h
 * @brief   qtmonitor's main window and its startup options.
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMap>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QPromise>
#include "tagclass.h"
#include "ui_mainwindow.h"

class QWidget;
class QTextEdit;
class QGroupBox;
class ConfigTab;



/// The Error Log tab's text box, defined in logwindow.cpp; null until created.
extern QTextEdit *s_textEdit;
class LogScreen;

namespace Ui {
class MainWindow;
}

/**
 * @brief Startup options for live qtmonitor and fixture screenshot modes.
 *
 * @details The default options preserve normal live operation: qtmonitor probes
 *          USB, attaches to a real tag, and polls status. Documentation
 *          automation supplies a fake fixture path and capture flags so the
 *          same MainWindow widgets can be populated from captured protobuf JSON
 *          without a connected tag.
 */
struct MainWindowOptions {
  /// Fixture JSON path for documentation replay. Empty means live tag mode.
  QString fakeFixturePath;
  /// Named fixture status to display for single-state replay.
  QString fakeState = "idle";
  /// Destination directory for generated screenshots.
  QString screenshotDir;
  /// Suppress startup USB probing, primarily for disconnected screenshots.
  bool skipAutoAttach = false;
  /// Capture the no-tag Tag State screen and exit.
  bool captureStartupScreenshot = false;
  /// Capture idle/running/finished Tag State screens from a fixture and exit.
  bool captureMainScreenshots = false;
  /// Capture the idle Configuration tab from a fixture and exit.
  bool captureConfigScreenshots = false;
  /// Capture the Error Log tab with no attached tag and exit.
  bool captureErrorLogScreenshot = false;
};

/**
 * @class   MainWindow
 * @brief   qtmonitor's window: Tag State, Configuration and Error Log tabs
 *          for one attached tag.
 *
 * @details In live mode it attaches through tagcore's Tag, polls the tag's
 *          status every 400 ms, and passes each state to the Configuration tab
 *          (ConfigTab) through StateUpdate(). In fixture mode it fills the
 *          same widgets from a captured fixture and issues no tag commands.
 */
class MainWindow : public QMainWindow {
  Q_OBJECT

public:

  /**
   * @brief Creates the qtmonitor main window in live or fixture mode.
   *
   * @details Default options preserve normal live operation. Fixture options
   *          load captured protobuf JSON and suppress USB operations so
   *          documentation screenshots can be generated without hardware.
   *
   * @param[in] options   Startup and screenshot-capture behavior.
   * @param[in] parent    Optional Qt parent widget.
   */
  explicit MainWindow(const MainWindowOptions &options = MainWindowOptions(),
                      QWidget *parent = nullptr);
  ~MainWindow();
  /**
   * @brief Reports whether startup was requested only to run a capture slot.
   *
   * @return true when a screenshot option should schedule capture and exit;
   *         false for normal interactive qtmonitor operation.
   */
  bool shouldQuitAfterStartup() const;

public slots:

  /**
   * @brief Capture the no-tag startup state for documentation.
   */
  void captureStartupScreenshot();

  /**
   * @brief Capture fake fixture Tag State screenshots for idle/running/finished.
   */
  void captureMainScreenshots();

  /**
   * @brief Capture fake fixture Configuration screenshots in idle mode.
   */
  void captureConfigScreenshots();

  /**
   * @brief Capture the Error Log tab for documentation.
   */
  void captureErrorLogScreenshot();
 

signals:

  /// Every status poll: the tag's current state. Connected to ConfigTab::StateUpdate().
  void StateUpdate(TagState state);
  /// During an erase: sectors erased so far.
  void SectorsErased(int);
  /// During an erase: sectors erased so far and the total to erase.
  void EraseProgress(int value, int maximum);
  /// A status poll found the tag IDLE; closes the erase progress dialog.
  void IdleState(void);

private slots:

  // attach/detach to tag

  bool Attach();

  // timer tick

  void TriggerUpdate();

  // control tab events

  void on_syncButton_clicked();     // synchronize clock
  void on_stopButton_clicked();     // stop tag
 
  void on_eraseButton_clicked();    // reset tag flash
  void on_testButton_clicked();     // run tag self-test
  void on_calibrateButton_clicked(); // enter calibration state

  void on_Attach_clicked();         // attach to tag
  void on_Detach_clicked();         // detach from tag

  // data events

  void on_tagLogSaveButton_clicked(); // download data from tag

  

private:
  Tag tag;
  TagType tag_type = TAG_UNSPECIFIED;
  Ui::MainWindow ui;
  QTimer timer;
  TagState current_state = STATE_UNSPECIFIED;
  const float version = 2.0;
  int external_flash_size = 0;
  int sector_size = 4096;
  QString tag_debug_line_buffer;
  int eraseSectorMaximum(const Status &status) const;
  void logTagDebugMessage(const QString &message);
  /**
   * @brief Enables or disables the top-level groups that require an attachment.
   */
  void setAttachedUiEnabled(bool enabled);
  /**
   * @brief Copies tag identity metadata into the Tag Information widgets.
   */
  void populateTagInfo(const TagInfo &info);
  /**
   * @brief Applies a configuration to the Configuration tab.
   *
   * @details Live mode attaches the tab to @c tag before setting the config.
   *          Fixture mode skips the live Tag attachment and puts the tab in a
   *          display-only mode where Start and Read are no-ops.
   */
  void attachConfig(const Config &config, bool fixtureMode);
  /**
   * @brief Applies status-dependent text, counts, and control availability.
   */
  void applyStatus(const Status &status, float voltage);
  /**
   * @brief Loads protobuf JSON fixture data for documentation replay mode.
   */
  bool loadFakeFixture(const QString &path);
  /**
   * @brief Populates qtmonitor from the loaded fixture without USB attachment.
   */
  bool setupFakeFixture();
  /**
   * @brief Returns a named fixture status, deriving display states if needed.
   */
  Status fakeStatusForName(const QString &name) const;
  /**
   * @brief Applies one named fixture status to the live widgets.
   */
  bool applyFakeState(const QString &name);
  /**
   * @brief Saves a native-frame screenshot of the current qtmonitor window.
   */
  bool saveCurrentScreenshot(const QString &path,
                             const QSize &maximumWindowSize = QSize(),
                             const QSize &maximumImageSize = QSize());
  /**
   * @brief Resolves a generated screenshot filename under the capture directory.
   */
  QString screenshotPath(const QString &filename) const;

  QString screenshotDir;
  QString fakeFixturePath;
  QString fakeFixtureId;
  TagInfo fakeInfo;
  Config fakeConfig;
  QMap<QString, Status> fakeStatuses;
  float fakeVoltage = 0.0f;
  bool fakeMode = false;
  bool fakeConfigLoaded = false;
  bool captureStartupScreenshotOnStartup = false;
  bool captureMainScreenshotsOnStartup = false;
  bool captureConfigScreenshotsOnStartup = false;
  bool captureErrorLogScreenshotOnStartup = false;
};

#endif // MAINWINDOW_H
