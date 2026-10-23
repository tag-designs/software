/**
 * @file    mainwindow.h
 * @brief   qtmonitor's main window and its startup options.
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDateTime>
#include <QMap>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QPromise>
#include "tagclass.h"
#include "ui_mainwindow.h"

class QAction;
class QTabBar;
class QWidget;
class QTextEdit;
class QGroupBox;
class ConfigTab;
class TagStatusStrip;



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

  /**
   * @brief Which actions updateActions() enables, for every combination of
   *        the inputs it reads; for `qtmonitor --check-action-states`.
   *
   * @details Sets each combination of attached, tag state, tag type (one
   *          with calibration, one without), configuration loaded, download
   *          running and self-test pending, calls updateActions(), and records
   *          each action as E (enabled), D (disabled) or H (hidden). Leaves the
   *          window detached. See host/applications/qtmon/test/README.md.
   *
   * @return One header line, then one line per combination.
   */
  QString actionStateTable();

public slots:

  /**
   * @brief Capture the no-tag startup state for documentation.
   */
  void captureStartupScreenshot();

  /**
   * @brief Capture fake fixture Tag tab screenshots for idle/running/finished.
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

  // Tag commands, each behind an action (see createActions()).

  void attachTag();      ///< Tag ▸ Attach: probe USB bases and attach.
  void detachTag();      ///< Tag ▸ Detach.
  void syncClock();      ///< Tag ▸ Synchronize Clock: set the tag's RTC.
  void stopTag();        ///< Tag ▸ Stop, and the Stop button.
  void testTag();        ///< Tag ▸ Self-Test: run every self-test.
  void calibrateTag();   ///< Tag ▸ Calibrate: enter the calibration state.
  void eraseTag();       ///< Tag ▸ Erase…: confirm, erase, show progress.
  void downloadData();   ///< File ▸ Download Data…: save the log as SQLite.

  

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

  /**
   * @brief Builds every action and connects it to its command.
   *
   * @details Each command is one QAction, so the menus, the popup menus and
   *          the status strip's Start and Stop buttons share one enabled
   *          state, which updateActions() sets.
   */
  void createActions();
  /// Builds the File, Tag and Log menus and the popup menus.
  void createMenus();
  /**
   * @brief Puts the status strip between the tab bar and the pages.
   *
   * @details Hides mainTabWidget's own tab bar, which QTabWidget draws on top
   *          of its pages, and adds a standalone QTabBar above the strip, kept
   *          in step with mainTabWidget in both directions.
   */
  void placeStatusStripBelowTabs();
  /**
   * @brief Enables, disables and shows the actions for the current state.
   *
   * @details The single place where command availability is decided, from
   *          whether a tag is attached, its state and type, whether a download
   *          or self-test is in progress, and whether the Configuration tab
   *          holds a configuration. Called after every change to those.
   */
  void updateActions();

  QTabBar *tab_bar_ = nullptr;  ///< Stands in for mainTabWidget's hidden tab bar.
  TagStatusStrip *status_strip_ = nullptr;  ///< Tag, state, clock, voltage, self-test; Start and Stop.
  bool attached_ui_ = false;   ///< A live or fixture tag is attached (setAttachedUiEnabled()).
  bool downloading_ = false;   ///< A data download is running.
  bool test_pending_ = false;  ///< Self-Test sent; result not yet seen (see applyStatus()).
  QDateTime test_started_;     ///< When Self-Test was sent.
  QString tag_uuid_;           ///< Attached tag's board UUID, for log messages.

  QAction *attach_action_ = nullptr;
  QAction *detach_action_ = nullptr;
  QAction *read_config_action_ = nullptr;
  QAction *start_action_ = nullptr;
  QAction *stop_action_ = nullptr;
  QAction *sync_action_ = nullptr;
  QAction *test_action_ = nullptr;
  QAction *calibrate_action_ = nullptr;
  QAction *erase_action_ = nullptr;
  QAction *save_config_action_ = nullptr;
  QAction *restore_config_action_ = nullptr;
  QAction *download_action_ = nullptr;
  QAction *quit_action_ = nullptr;

  /// A self-test result is accepted only from a poll this long after Self-Test.
  static constexpr qint64 kTestMinimumMs = 1000;
  /// Self-Test gives up waiting for a result after this long.
  static constexpr qint64 kTestTimeoutMs = 60000;

  int eraseSectorMaximum(const Status &status) const;
  void logTagDebugMessage(const QString &message);
  /**
   * @brief Records whether a tag is attached, and updates the Tag page, the
   *        status strip and the actions to match.
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
  /**
   * @brief Resolves the directory screenshots are written to; see
   *        mainwindow_fixture.cpp.
   */
  static QString resolveScreenshotDir(const QString &path);

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
