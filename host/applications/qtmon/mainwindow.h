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
class QMenu;
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
   *          with calibration, one without), configuration document (none, of
   *          the tag's type, of another type), download
   *          running and self-test pending, calls updateActions(), and records
   *          each action as E (enabled), D (disabled) or H (hidden). Leaves the
   *          window detached. See host/applications/qtmon/test/README.md.
   *
   * @return One header line, then one line per combination.
   */
  QString actionStateTable();

  /**
   * @brief Which start checks fail, for every combination of their inputs;
   *        for `qtmonitor --check-start-checks`.
   *
   * @details Sets each combination of clock offset, reported self-test
   *          result and approval, and records startCheckFailures(). See
   *          host/applications/qtmon/test/README.md.
   *
   * @return One header line, then one line per combination.
   */
  QString startCheckTable();

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

  /**
   * @brief Finds the USB bases (asking which when there are several),
   *        attaches, reads the tag's identity, configuration and status, and
   *        starts polling every 400 ms.
   * @return true when attached; false, after telling the user, when already
   *         attached, no base is found or chosen, or the tag does not answer.
   * @warning Attaching resets the tag, ending a running deployment.
   */
  bool Attach();

  // timer tick

  /**
   * @brief One status poll: voltage and Status into applyStatus(); in
   *        CALIBRATE, also drains the calibration log into the calibration
   *        window.
   */
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
  void showUserGuide();  ///< Help ▸ Tag Monitor User Guide: open the manual page.
  void showAbout();      ///< Help ▸ About Tag Monitor.
  /**
   * @brief Tag ▸ Start and the Start button: runs the start checks, warns
   *        about any that fail, and starts the tag unless the user cancels.
   */
  void startWithChecks();

  

private:
  Tag tag;                    ///< Connection to the attached tag.
  TagType tag_type = TAG_UNSPECIFIED;  ///< Attached tag's type, from TagInfo.
  Ui::MainWindow ui;          ///< The Designer-built widgets (mainwindow.ui).
  QTimer timer;               ///< Status poll (TriggerUpdate()), every 400 ms while attached.
  TagState current_state = STATE_UNSPECIFIED;  ///< Tag state at the last poll.
  /// Shown in the title and About. 2.1: menus, the status strip, Help.
  const float version = 2.1f;
  int external_flash_size = 0;  ///< Bytes, from TagInfo; sizes the erase progress bar.
  int sector_size = 4096;       ///< External flash erase sector, bytes.
  /// Firmware debug text received so far without its closing newline (see logTagDebugMessage()).
  QString tag_debug_line_buffer;

  /**
   * @brief Builds every action and connects it to its command.
   *
   * @details Each command is one QAction, so the menus, the popup menus and
   *          the status strip's Start and Stop buttons share one enabled
   *          state, which updateActions() sets.
   */
  void createActions();
  /// Builds the File, Tag, Log and Help menus and the popup menus.
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
  /**
   * @brief The start checks that fail now, one line each, empty when all pass.
   *
   * @details Start warns, and asks whether to continue, when the clock at the
   *          latest status is more than TagStatusStrip::kClockToleranceS from
   *          this computer's, when the tag's reported self-test result is not
   *          ALL_PASSED, or when "Approved" is not ticked.
   */
  QStringList startCheckFailures() const;
  /// Shows the configuration document's name and unsaved-changes mark in the title.
  void updateTitle();
  /// Lists the recent configuration files in File ▸ Open Recent.
  void fillRecentMenu();
  /// Asks about unsaved configuration changes before the window closes.
  void closeEvent(QCloseEvent *event) override;

  QTabBar *tab_bar_ = nullptr;  ///< Stands in for mainTabWidget's hidden tab bar.
  TagStatusStrip *status_strip_ = nullptr;  ///< Tag, state, clock, voltage, self-test; Start and Stop.
  bool attached_ui_ = false;   ///< A live or fixture tag is attached (setAttachedUiEnabled()).
  bool downloading_ = false;   ///< A data download is running.
  bool test_pending_ = false;  ///< Self-Test sent; result not yet seen (see applyStatus()).
  QDateTime test_started_;     ///< When Self-Test was sent.
  double clock_offset_s_ = 0.0;           ///< Tag clock minus host clock at the last status.
  TestResult last_test_ = TEST_UNSPECIFIED;  ///< Self-test result the last status reported.
  QString tag_uuid_;           ///< Attached tag's board UUID, for log messages.

  // The commands, made by createActions() and enabled by updateActions().
  QAction *attach_action_ = nullptr;        ///< Tag ▸ Attach.
  QAction *detach_action_ = nullptr;        ///< Tag ▸ Detach.
  QAction *read_config_action_ = nullptr;   ///< Tag ▸ Read Configuration.
  QAction *start_action_ = nullptr;         ///< Tag ▸ Start and the Start button; runs startWithChecks().
  QAction *stop_action_ = nullptr;          ///< Tag ▸ Stop and the Stop button.
  QAction *sync_action_ = nullptr;          ///< Tag ▸ Synchronize Clock.
  QAction *test_action_ = nullptr;          ///< Tag ▸ Self-Test.
  QAction *calibrate_action_ = nullptr;     ///< Tag ▸ Calibrate; hidden for tags without calibration.
  QAction *erase_action_ = nullptr;         ///< Tag ▸ Erase….
  QAction *open_config_action_ = nullptr;   ///< File ▸ Open Configuration….
  QAction *save_config_action_ = nullptr;   ///< File ▸ Save Configuration.
  QAction *save_as_config_action_ = nullptr; ///< File ▸ Save Configuration As….
  QAction *reset_config_action_ = nullptr;  ///< Reset to Defaults.
  QMenu *new_config_menu_ = nullptr;     ///< One action per tag type with defaults.
  QMenu *recent_config_menu_ = nullptr;  ///< Rebuilt each time it opens.
  QAction *download_action_ = nullptr;  ///< File ▸ Download Data….
  QAction *quit_action_ = nullptr;      ///< File ▸ Quit; closes the window, which may ask to save.
  QAction *guide_action_ = nullptr;     ///< Help ▸ Tag Monitor User Guide.
  QAction *about_action_ = nullptr;     ///< Help ▸ About Tag Monitor.

  /// A self-test result is accepted only from a poll this long after Self-Test.
  static constexpr qint64 kTestMinimumMs = 1000;
  /// Self-Test gives up waiting for a result after this long.
  static constexpr qint64 kTestTimeoutMs = 60000;

  /**
   * @brief Number of external-flash sectors the tag's erase will report, for
   *        the erase progress bar.
   * @return The tag's own total when it reports one
   *         (erase_sectors_total_plus_one); otherwise an estimate from the
   *         IMUTag page count or the external flash size.
   */
  int eraseSectorMaximum(const Status &status) const;
  /**
   * @brief Logs firmware debug text one line at a time, as "Log: <line>".
   * @details The firmware's text arrives in pieces across statuses; this
   *          drops carriage returns and holds an unfinished line in
   *          tag_debug_line_buffer until its newline arrives.
   */
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
   * @details Live mode attaches the tab to @c tag. A configuration document
   *          the user opened or created (from a file or defaults) is kept;
   *          otherwise the tag's configuration becomes the document, asking
   *          first if a previous tag's has unsaved changes. Fixture mode
   *          always shows @p config, in a display-only mode where Start and
   *          Read are no-ops.
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

  // Documentation fixture mode (--fake-fixture) and screenshot capture; see
  // mainwindow_fixture.cpp and README.md "Screenshots".
  QString screenshotDir;        ///< Where captures are written (resolveScreenshotDir()).
  QString fakeFixturePath;      ///< The fixture JSON, when running without a tag.
  QString fakeFixtureId;        ///< Fixture name used in screenshot file names.
  TagInfo fakeInfo;             ///< Tag identity from the fixture.
  Config fakeConfig;            ///< Tag configuration from the fixture.
  QMap<QString, Status> fakeStatuses;  ///< Fixture statuses by lower-case state name ("idle", "running", ...).
  float fakeVoltage = 0.0f;     ///< Voltage shown in fixture mode, volts.
  bool fakeMode = false;        ///< A fixture stands in for an attached tag; no tag is contacted.
  bool fakeConfigLoaded = false; ///< fakeConfig was read from the fixture.
  bool captureStartupScreenshotOnStartup = false;   ///< --capture-startup-screenshot.
  bool captureMainScreenshotsOnStartup = false;     ///< --capture-main-screenshots.
  bool captureConfigScreenshotsOnStartup = false;   ///< --capture-config-screenshots.
  bool captureErrorLogScreenshotOnStartup = false;  ///< --capture-error-log-screenshot.
};

#endif // MAINWINDOW_H
