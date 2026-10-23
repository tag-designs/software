/**
 * @file    configtab.h
 * @brief   qtmonitor's Configuration tab: a page of fixed panels, selected by
 *          the attached tag's type, plus save, restore, read and start.
 *
 * @see     tagconfiguration/README.md for the panels and how to add tags,
 *          fields and panels; docs/shared/design/configuration-model.md for
 *          the configuration model as a whole.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <QWidget>
//#include <QDateTime>
#include <QTabWidget>
#include <QPushButton>
#include <QList>
#include <QMessageBox>
//#include "host.pb.h"
//#include "tagclass.h"
#include <QStringList>
#include <vector>

#include "configpanel.h"
#include "ui_configtab.h"
#include "tagclass.h"

/**
 * @class   ConfigTab
 * @brief   Shows, edits and sends the attached tag's configuration.
 *
 * @details Every ConfigPanel is built once, in the constructor, and placed on
 *          a single scrolling page. SetConfig() shows the panels the tag type
 *          selects (`panelsForTag()` in configtab.cpp) and loads each from the
 *          configuration; GetConfig() starts from that configuration and lets
 *          each shown panel write the fields it owns, so fields no panel owns
 *          pass through unchanged. Save and Start both use GetConfig().
 *
 *          Live qtmonitor calls Attach() before SetConfig(), so Read and Start
 *          can reach the tag. Fixture mode (SetFixtureMode()) and the
 *          configuration probe use the tab without a tag.
 *
 *          Not thread-safe: use from the GUI thread only.
 */
class ConfigTab :  public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Builds the page with every panel hidden.
     * @param[in] parent  Qt parent.
     */
    explicit ConfigTab(QWidget *parent = nullptr);
    ~ConfigTab();

    /**
     * @brief The configuration Start and Save would send.
     *
     * @details Copies the current configuration, sets its tag type, and has
     *          each shown panel write its fields from its controls, whatever
     *          is on screen. On success the result also becomes the current
     *          configuration.
     *
     * @param[out] config  Receives the configuration.
     * @return false when a panel holds a value it cannot write (only the
     *         BitTag log panel can); @p config must then not be sent.
     */
    bool GetConfig(Config &config);

    /**
     * @brief Displays a configuration, normally the tag's own.
     *
     * @details Stores @p config as the current configuration, shows the panels
     *          its tag type selects, loads each from it, and sizes the page.
     *          A tag type with no panels shows an empty page. Shows the tab and
     *          enables it; StateUpdate() then decides what is editable.
     *
     * @param[in] config  Configuration to display.
     * @return Always true.
     *
     * @note The BitTag log panel shows a modal warning when a classic BitTag
     *       reports no log format.
     */
    bool SetConfig(const Config &config);

    /// True after SetConfig(), false after Detach().
    bool isActive();
    /**
     * @brief Puts the configuration tab in read-only fixture display mode.
     *
     * @details Fixture mode is used by qtmonitor documentation screenshots.
     *          It allows SetConfig() and StateUpdate() to populate and enable
     *          the real tag-specific widgets while blocking actions that would
     *          require a live Tag object, such as Read and Start.
     */
    void SetFixtureMode(bool enabled);

    /**
     * @brief Outcome of RestoreFromJsonText().
     */
    enum class RestoreResult {
        Restored,        ///< The file was merged and applied.
        ParseError,      ///< The text is not a valid Config; nothing changed.
        NotValidForTag,  ///< Parsed and merged, but SetConfig() rejected it.
    };

    /**
     * @brief Overlays a configuration file's contents onto the current
     *        configuration, as the Restore button does, without dialogs.
     *
     * @details Copies each field that the attached tag supports and the file
     *          speaks for (ConfigShape::forFile() and ConfigShape::merge()): a
     *          field the file leaves out keeps its value, a stated field is
     *          copied (zero included), and a file whose `tag_type` is another
     *          tag type contributes only that type's fields. Then redisplays
     *          with SetConfig().
     *
     * @param[in]  json_text  Contents of a configuration file.
     * @param[out] detail     Parser status or rejected configuration text on
     *                        failure. May be null.
     * @return The outcome; see RestoreResult.
     */
    RestoreResult RestoreFromJsonText(const std::string &json_text,
                                      QString *detail);

    /**
     * @brief Checks which panels each tag type shows against every tag
     *        type's ConfigShape; run by `qtmonitor --check-config-panels`.
     *
     * @return One line per mismatch; empty when they agree.
     */
    QStringList CheckPanelsAgainstShapes() const;

public slots:

    /**
     * @brief Remembers the live tag that Read and Start talk to.
     * @param[in] tag  Attached tag; must outlive its use by this tab.
     * @return Always true.
     */
    bool Attach(Tag &tag);

    /**
     * @brief Hides every panel and the tab, and marks it inactive, when the
     *        tag is detached.
     */
    void Detach();

    /**
     * @brief Enables editing only while the tag is IDLE.
     *
     * @details In IDLE the shown panels are enabled; in any other state they
     *          are disabled, so a running tag's configuration is displayed but
     *          not editable. MainWindow enables the configuration actions
     *          (Restore, Read, Start) by the same rule.
     *
     * @param[in] state  The tag's current state.
     */
    void StateUpdate(TagState state);

    /**
     * @brief Asks for a file and saves the configuration Start would send
     *        (GetConfig()) to it as protobuf JSON, every field printed.
     */
    void SaveToFile();

    /**
     * @brief Asks for a file and restores it over the current configuration
     *        (RestoreFromJsonText()), reporting any failure in a dialog.
     */
    void RestoreFromFile();

    /**
     * @brief Writes the configuration to the attached tag and starts it,
     *        then polls until the tag leaves IDLE (see pollStartResult()).
     *        A no-op in fixture mode.
     */
    void StartTag();

    /**
     * @brief Reads the attached tag's configuration and displays it. A no-op
     *        in fixture mode.
     */
    void ReadFromTag();

private:

    /**
     * @brief Poll for the tag's post-Start state, retrying on a timer.
     *
     * @details Tag::Start() returning false does not mean the tag never
     *          started: a transient monitor/USB error (LIBUSB timeout, a
     *          failed debug-register access) can lose the acknowledgement
     *          after the tag already accepted the command, the same
     *          "acknowledgement is not a completion" gap tag-start/tag-stop
     *          poll around on the command line. Polls GetStatus() a bounded
     *          number of times, 1 s apart, via a non-blocking QTimer chain
     *          (never a sleep loop -- that would freeze the GUI thread).
     *          RUNNING or CONFIGURED at any poll is treated as success, even
     *          if the initial Start() call itself reported failure; only
     *          still-IDLE (or another unexpected state) after the last
     *          attempt shows the failure dialog, using the freshest state
     *          read rather than whatever was captured immediately after the
     *          original failed call.
     *
     * @param[in] start_call_message DebugMessage() from a failed Start()
     *                                call, or empty if Start() returned true.
     * @param[in] attempt Zero-based retry count so far.
     */
    void pollStartResult(std::string start_call_message, int attempt);

    /// Sizes the scroll area to the shown panels; see the definition.
    void FitPanelPage();
    /// Height left for the main window's tabs, buttons and frame (pixels).
    static constexpr int kWindowChrome = 260;

    // Helper function

    TagType tag_type_ = TAG_UNSPECIFIED;  ///< Tag type of the current configuration.
    TagState old_state_ = STATE_UNSPECIFIED;  ///< Last state from StateUpdate() or a Start poll.
    Config current_config_;  ///< The tag's configuration as last read, restored or sent.
    bool fixture_mode_ = false;  ///< Read and Start are no-ops; see SetFixtureMode().

    /// Every panel, built once in the constructor, in page order.
    std::vector<ConfigPanel *> panels_;
    /// The panels the current tag type shows, in page order.
    std::vector<ConfigPanel *> shown_;
    bool active = false;  ///< See isActive().

    // for errors

    QMessageBox msgBox;  ///< Reused for "Tag config read failed".
    Tag *tag;            ///< Live tag from Attach(). Uninitialised before then; fixture mode never uses it.
    Ui::ConfigTab ui;    ///< Page, scroll area and file/tag buttons.
    
};

#endif // CONFIG_H
