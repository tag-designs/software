/**
 * @file    mainwindow.h
 * @brief   qtmonitor's main window and its startup options.
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <array>
#include <memory>

#include <QMainWindow>
#include <QPointer>
#include <QSize>
#include <QString>

#include "commands.h"
#include "menus.h"
#include "monitormodel.h"
#include "tagsession.h"
#include "ui_mainwindow.h"

class QAction;
class QMenu;
class QProgressDialog;
class QTabBar;
class QTimer;
class QTextEdit;
class ConfigTab;
class FixtureSession;
class TagStatusStrip;

/// The Error Log tab's text box, defined in logwindow.cpp; null until created.
extern QTextEdit *s_textEdit;

namespace Ui {
class MainWindow;
}

/**
 * @brief Startup options for live qtmonitor and fixture screenshot modes.
 *
 * @details The default options give normal live operation: qtmonitor probes
 *          USB, attaches to a real tag, and polls its status. Documentation
 *          automation supplies a fixture path and capture flags so the same
 *          window is filled from captured protobuf JSON with no tag.
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
 * @details The window is the view and the place commands start. It never
 *          touches a tag: it sends each command to a TagSession
 *          (session/tagsession.h), whose reports it feeds to a
 *          qtmon::MonitorModel (core/monitormodel.h), and then draws the
 *          model in render(). Which commands are available is decided by
 *          qtmon::availability() (core/commands.h); the configuration
 *          document is the Configuration tab's (ConfigTab). In live mode the
 *          session is a LiveTagSession, whose worker thread owns the tag, so
 *          the window never waits on it; in fixture mode it is a
 *          FixtureSession replaying a capture. See
 *          host/applications/qtmon/design/architecture.md.
 */
class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  /**
   * @brief Creates the window, and its session: a FixtureSession when
   *        @p options names a fixture, a LiveTagSession otherwise.
   *
   * @details In live mode it then finds the tag bases and attaches, unless
   *          @p options says not to.
   *
   * @param[in] options   Startup and screenshot-capture behaviour.
   * @param[in] parent    Optional Qt parent widget.
   */
  explicit MainWindow(const MainWindowOptions &options = MainWindowOptions(),
                      QWidget *parent = nullptr);
  ~MainWindow();

  /**
   * @brief Reports whether startup was requested only to run a capture slot.
   * @return true when a screenshot option should schedule capture and exit.
   */
  bool shouldQuitAfterStartup() const;

  /**
   * @brief Checks that the window applies the command rule to the right
   *        actions; for `qtmonitor --check-action-wiring`.
   *
   * @details Applies every case of qtmon::actionStateCases() to the actions
   *          (applyCommandInputs(), the same path updateActions() takes) and
   *          compares each action's visibility and enabled state with
   *          qtmon::availability(). Each action is found by its menu text,
   *          not through actionFor(), so a wrong entry there is caught. The
   *          rule itself is checked by `--check-action-states`. Leaves the
   *          actions as the model says.
   *
   * @return One line per disagreement; empty when every action agrees.
   */
  QStringList checkActionWiring();

  /**
   * @brief The window's menus as text; for `qtmonitor --check-menus`.
   *
   * @details Every menu in the menu bar, depth first, then the popup menus of
   *          the Tag page, the status strip and the Configuration tab: one line
   *          per entry with its text, shortcut and tooltip, `---` for a
   *          separator, and a submenu's entries indented under it. Open
   *          Recent is filled only when it opens, so it lists none. Compared
   *          with test/menus.txt.
   *
   * @return The text, one line per entry, ending with a newline.
   */
  QString menuTable();

public slots:
  /// Capture the no-tag startup state for documentation.
  void captureStartupScreenshot();
  /// Capture fixture Tag tab screenshots for idle, running and finished.
  void captureMainScreenshots();
  /// Capture the fixture's idle Configuration tab.
  void captureConfigScreenshots();
  /// Capture the Error Log tab for documentation.
  void captureErrorLogScreenshot();

private slots:
  // Commands, each behind an action (see createActions()).

  /**
   * @brief Tag ▸ Attach: asks the session for the tag bases; attaching
   *        continues in onBasesFound().
   * @warning Attaching resets the tag, ending a running deployment.
   */
  void attachTag();
  void detachTag();      ///< Tag ▸ Detach.
  void readConfig();     ///< Tag ▸ Read Configuration: asks about unsaved changes, then reads.
  void syncClock();      ///< Tag ▸ Synchronize Clock.
  void stopTag();        ///< Tag ▸ Stop, and the Stop button.
  void testTag();        ///< Tag ▸ Self-Test.
  void eraseTag();       ///< Tag ▸ Erase…: confirms, then erases.
  void downloadData();   ///< File ▸ Download Data…: asks for a file, then downloads.
  void showUserGuide();  ///< Help ▸ Tag Monitor User Guide.
  void showAbout();      ///< Help ▸ About Tag Monitor.
  /**
   * @brief Tag ▸ Start and the Start button: runs the start checks, warns
   *        about any that fail, and sends the configuration unless the user
   *        cancels.
   */
  void startWithChecks();

  // Session reports, on the GUI thread.

  /// Chooses a base (asking when there are several) and attaches to it.
  void onBasesFound(std::vector<UsbDev> bases);
  /// A tag is attached: records it and loads its configuration (attachConfig()).
  void onAttached(TagInfo info, Config config);
  /// Attaching failed: says so.
  void onAttachFailed(QString message);
  /// The tag is detached: forgets it.
  void onDetached();
  /// A status: feeds the model, reports what it settled, and redraws.
  void onStatus(Status status, float voltage, qint64 host_ms);
  /// The tag's configuration, read for Read Configuration, becomes the document.
  void onConfigRead(Config config);
  /// Start was sent; the model judges it from the statuses that follow.
  void onStartSent(QString call_message);
  /// Self-Test was accepted.
  void onTestSent();
  /// Erase was accepted: shows its progress until the tag is IDLE.
  void onEraseSent();
  /// The tag refused a command: says so.
  void onCommandFailed(qtmon::Command command, QString message);
  /// Download progress, for its dialog.
  void onDownloadProgress(int value, int maximum);
  /// The download ended: closes its dialog and reports a failure.
  void onDownloadFinished(bool ok, QString error);

private:
  /**
   * @brief Builds every action from the table in core/menus.h: one per
   *        qtmon::Command, run by runCommand(), and the window's own items
   *        (New Configuration, Open Recent, Quit, the user guide, About).
   */
  void createActions();
  /// One action labelled, described and given a shortcut as @p text says.
  QAction *makeAction(const qtmon::EntryText &text);
  /// Runs @p command: the slot or Configuration tab command behind it.
  void runCommand(qtmon::Command command);
  /// The action for one table entry: a command's, an item's, or a new separator.
  QAction *entryAction(const qtmon::MenuEntry &entry);
  /// Builds the menu bar and the popup menus from the tables in core/menus.h.
  void createMenus();
  /**
   * @brief Puts the status strip between the tab bar and the pages.
   *
   * @details Hides mainTabWidget's own tab bar, which QTabWidget draws on top
   *          of its pages, and adds a standalone QTabBar above the strip, kept
   *          in step with mainTabWidget in both directions.
   */
  void placeStatusStripBelowTabs();
  /// Connects the session's reports to the on...() slots.
  void connectSession();

  /**
   * @brief Draws the model: the Tag page, the status strip, the
   *        Configuration tab's editability, the actions and the erase
   *        progress. Called after every change to the model.
   */
  void render();
  /**
   * @brief Enables, disables and shows the actions from the model and the
   *        document, through qtmon::availability().
   */
  void updateActions();
  /// Applies qtmon::availability() for @p in to every action.
  void applyCommandInputs(const qtmon::CommandInputs &in);
  /// The action that carries @p command.
  QAction *actionFor(qtmon::Command command) const;
  /**
   * @brief The start checks that fail now, one sentence each, empty when all
   *        pass: qtmon::startCheckFailures() (core/startchecks.h) for the
   *        latest clock offset and self-test result and the document's
   *        approval.
   */
  QStringList startCheckFailures() const;
  /// Shows the configuration document's name and unsaved-changes mark in the title.
  void updateTitle();
  /// Lists the recent configuration files in File ▸ Open Recent.
  void fillRecentMenu();
  /// Asks about unsaved configuration changes before the window closes.
  void closeEvent(QCloseEvent *event) override;
  /// Copies the tag's identity into the Tag Information group and the strip.
  void populateTagInfo(const TagInfo &info);
  /**
   * @brief Loads an attached tag's configuration into the Configuration tab.
   *
   * @details A document the user opened or created (from a file or
   *          defaults) is kept, since they may be about to start this tag
   *          with it; a previous tag's configuration is replaced, asking
   *          first if it has unsaved changes.
   */
  void attachConfig(const Config &config);
  /// "<tag type> <uuid>", for log messages.
  QString tagName() const;
  /**
   * @brief Seconds since a status got through, once that is at least
   *        qtmon::kNoAnswerMs; 0 while the tag answers, and always with a
   *        fixture session.
   */
  qint64 silenceS() const;

  // ---- Screenshot capture (mainwindow_fixture.cpp) ----

  /// Saves a native-frame screenshot of the window.
  bool saveCurrentScreenshot(const QString &path, const QSize &maximumWindowSize = QSize(),
                             const QSize &maximumImageSize = QSize());
  /// A screenshot file name under the capture directory.
  QString screenshotPath(const QString &filename) const;
  /// The directory screenshots are written to; see mainwindow_fixture.cpp.
  static QString resolveScreenshotDir(const QString &path);

  Ui::MainWindow ui;  ///< The Designer-built widgets (mainwindow.ui).
  /// Shown in the title and About. 2.1: menus, the status strip, Help.
  const float version = 2.1f;

  std::unique_ptr<TagSession> session_;  ///< The tag: live or fixture.
  FixtureSession *fixture_ = nullptr;    ///< session_, when it is a fixture; not owned.
  qtmon::MonitorModel model_;            ///< What the window knows about the tag.

  QTabBar *tab_bar_ = nullptr;  ///< Stands in for mainTabWidget's hidden tab bar.
  TagStatusStrip *status_strip_ = nullptr;  ///< Tag, state, clock, voltage, self-test; Start and Stop.
  QPointer<QProgressDialog> erase_progress_;     ///< Shown while the model is erasing.
  QPointer<QProgressDialog> download_progress_;  ///< Shown while a download runs.
  /// Redraws once a second with a live session, so a tag that stops
  /// answering is shown as such (qtmon::kNoAnswerMs); null with a fixture.
  QTimer *silence_timer_ = nullptr;

  /// Each command's action, by qtmon::Command; made by createActions() from
  /// the table in core/menus.h, enabled by updateActions().
  std::array<QAction *, qtmon::kAllCommands.size()> command_actions_{};
  QMenu *new_config_menu_ = nullptr;     ///< File ▸ New Configuration: one action per tag type.
  QMenu *recent_config_menu_ = nullptr;  ///< File ▸ Open Recent; rebuilt each time it opens.
  QAction *quit_action_ = nullptr;      ///< File ▸ Quit; closes the window, which may ask to save.
  QAction *guide_action_ = nullptr;     ///< Help ▸ Tag Monitor User Guide.
  QAction *about_action_ = nullptr;     ///< Help ▸ About Tag Monitor.

  // Screenshot capture; see mainwindow_fixture.cpp and README.md "Screenshots".
  QString screenshotDir;  ///< Where captures are written (resolveScreenshotDir()).
  bool captureStartupScreenshotOnStartup = false;   ///< --capture-startup-screenshot.
  bool captureMainScreenshotsOnStartup = false;     ///< --capture-main-screenshots.
  bool captureConfigScreenshotsOnStartup = false;   ///< --capture-config-screenshots.
  bool captureErrorLogScreenshotOnStartup = false;  ///< --capture-error-log-screenshot.
};

#endif // MAINWINDOW_H
