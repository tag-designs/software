/**
 * @file    configtab.h
 * @brief   qtmonitor's Configuration tab: a configuration editor holding one
 *          document (from a tag type's defaults, a file, or a tag), shown as a
 *          page of fixed panels, with new, open, save, read and start.
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
#include <QLabel>
#include <vector>

#include "configpanel.h"
#include "ui_configtab.h"
#include "tagclass.h"

/**
 * @class   ConfigTab
 * @brief   The configuration editor: one configuration document, shown as a
 *          page of fixed panels, edited, saved, and started on a tag.
 *
 * @details The tab holds at most one **document**: a Config, its tag type
 *          (which selects the panels, `panelsForTag()` in configtab.cpp), its
 *          source (a tag type's defaults, a file, or a tag) and, for a file,
 *          its path. Every ConfigPanel is built once, in the constructor.
 *          Loading a document shows the panels its tag type selects and loads
 *          each from it; GetConfig() starts from the document and lets each
 *          shown panel write the fields it owns, so fields no panel owns pass
 *          through unchanged. Save and Start both use GetConfig().
 *
 *          Unsaved changes are detected by comparing what the panels would
 *          write with what they wrote just after loading; any control's change
 *          re-checks, and documentChanged() reports a change of state.
 *
 *          The tab works with no tag attached. Editing is disabled only while
 *          the document is the attached tag's own configuration and the tag
 *          is not IDLE, so a running tag's configuration is shown, not edited.
 *          Live qtmonitor calls Attach() and StateUpdate() so Read and Start
 *          can reach the tag; fixture mode (SetFixtureMode()) and the
 *          configuration probe use the tab without a tag.
 *
 *          Not thread-safe: use from the GUI thread only.
 */
class ConfigTab :  public QWidget {
    Q_OBJECT

public:
    /// Where the document came from.
    enum class Source {
        None,      ///< No document.
        Defaults,  ///< A tag type's default configuration (New).
        File,      ///< A configuration file (Open, or after Save).
        Tag,       ///< The attached tag's configuration (attach, Read).
    };

    /**
     * @brief Builds the page with every panel hidden and no document.
     * @param[in] parent  Qt parent.
     */
    explicit ConfigTab(QWidget *parent = nullptr);
    ~ConfigTab();

    /**
     * @brief The configuration Start and Save would send.
     *
     * @details Copies the document, sets its tag type, and has each shown
     *          panel write its fields from its controls, whatever is on
     *          screen. On success the result also becomes the document's
     *          configuration.
     *
     * @param[out] config  Receives the configuration.
     * @return false when there is no document, or a panel holds a value it
     *         cannot write (only the BitTag log panel can); @p config must
     *         then not be sent.
     */
    bool GetConfig(Config &config);

    /**
     * @brief Makes a tag's configuration the document (source Tag).
     *
     * @details Shows the panels its tag type selects and loads each from it,
     *          and sizes the page. A tag type with no panels shows an empty
     *          page. Does not ask about unsaved changes; callers do (see
     *          confirmDiscard()).
     *
     * @param[in] config  The tag's configuration.
     * @return Always true.
     *
     * @note The BitTag log panel shows a modal warning when a classic BitTag
     *       reports no log format.
     */
    bool SetConfig(const Config &config);

    /// True when a document is loaded.
    bool hasDocument() const { return source_ != Source::None; }
    /// The document's source.
    Source source() const { return source_; }
    /// The document's tag type; TAG_UNSPECIFIED with no document.
    TagType documentTagType() const { return hasDocument() ? tag_type_ : TAG_UNSPECIFIED; }
    /// The document's file, for source File; empty otherwise.
    QString filePath() const { return path_; }
    /// True when the panels would write something other than what was loaded.
    bool isModified() const;
    /**
     * @brief Short name for the window title: the file's name, "BITTAG_LE
     *        defaults", "BITTAG_LE from tag", or empty with no document.
     */
    QString documentName() const;

    /// Unloads the document, leaving the empty page. Does not ask.
    void Clear();

    /**
     * @brief Asks what to do with unsaved changes before they would be lost.
     *
     * @return true to go ahead: no unsaved changes, or the user saved them or
     *         chose to discard them; false when the user cancelled, or the
     *         save failed.
     */
    bool confirmDiscard();

    /**
     * @brief Puts the configuration tab in fixture display mode.
     *
     * @details Fixture mode is used by qtmonitor documentation screenshots.
     *          It allows SetConfig() and StateUpdate() to populate and enable
     *          the real tag-specific widgets while blocking actions that would
     *          require a live Tag object, such as Read and Start.
     */
    void SetFixtureMode(bool enabled);

    /// Outcome of OpenFromJsonText().
    enum class OpenResult {
        Opened,      ///< The file is now the document.
        ParseError,  ///< The text is not a valid Config; nothing changed.
        NoDefaults,  ///< Its tag type has no default config; nothing changed.
    };

    /**
     * @brief Makes a configuration file the document, without dialogs.
     *
     * @details The document is the tag type's defaults with the file merged
     *          on top (ConfigShape::forFile() and ConfigShape::merge()): a full
     *          file saved by qtmonitor loads as written, and a partial file
     *          still gives a complete configuration. The tag type is the
     *          file's `tag_type`, or @p fallback_type when it has none.
     *
     * @param[in]  json_text      Contents of a configuration file.
     * @param[in]  fallback_type  Tag type for a file that names none.
     * @param[in]  path           The file's path, kept for Save; may be empty.
     * @param[out] detail         Why it failed; may be null.
     * @return The outcome; see OpenResult.
     */
    OpenResult OpenFromJsonText(const std::string &json_text, TagType fallback_type,
                                const QString &path, QString *detail);

    /**
     * @brief Makes a tag type's default configuration the document, without
     *        asking about unsaved changes.
     * @return false, changing nothing, when the type has no default config.
     */
    bool LoadDefaults(TagType tag_type);

    /**
     * @brief Records the attached tag's type, offered first when opening a
     *        file that names none.
     */
    void SetAttachedTagType(TagType tag_type) { attached_tag_type_ = tag_type; }

    /// Recently opened or saved configuration files, newest first (QSettings).
    static QStringList recentFiles();

    /**
     * @brief Checks which panels each tag type shows against every tag
     *        type's ConfigShape; run by `qtmonitor --check-config-panels`.
     *
     * @return One line per mismatch; empty when they agree.
     */
    QStringList CheckPanelsAgainstShapes() const;

signals:
    /// The document was loaded, cleared, saved, or became modified or not.
    void documentChanged();
    /**
     * @brief The values to be sent may have changed: a control was changed,
     *        or a document was loaded, reset or cleared. Not emitted by Save,
     *        which changes no values. Clears the Start approval.
     */
    void contentChanged();

public slots:

    /**
     * @brief Remembers the live tag that Read and Start talk to.
     * @param[in] tag  Attached tag; must outlive its use by this tab.
     * @return Always true.
     */
    bool Attach(Tag &tag);

    /**
     * @brief Forgets the tag when it is detached. The document stays, and
     *        becomes editable if it was a running tag's configuration.
     */
    void Detach();

    /**
     * @brief Records the attached tag's state, and enables or disables
     *        editing to match (see the class description).
     *
     * @param[in] state  The tag's current state.
     */
    void StateUpdate(TagState state);

    /// File ▸ New Configuration ▸ \<type\>: asks about unsaved changes first.
    void NewFromDefaults(TagType tag_type);

    /**
     * @brief File ▸ Open Configuration…: asks about unsaved changes, then
     *        for a file, then opens it (OpenFile()).
     */
    void Open();

    /**
     * @brief Opens @p path as the document, asking about unsaved changes, and
     *        for a tag type when the file names none (the attached tag's type
     *        is offered first). Reports failures in a dialog.
     * @return true when the file is now the document.
     */
    bool OpenFile(const QString &path);

    /// File ▸ Save Configuration: to the document's file, or as Save As.
    bool Save();

    /// File ▸ Save Configuration As…: asks for a file, then saves to it.
    bool SaveAs();

    /// File ▸ Reset to Defaults: replaces the values with the tag type's defaults.
    void ResetToDefaults();

    /**
     * @brief Writes the configuration to the attached tag and starts it,
     *        then polls until the tag leaves IDLE (see pollStartResult()).
     *        A no-op in fixture mode.
     */
    void StartTag();

    /**
     * @brief Tag ▸ Read Configuration: makes the attached tag's configuration
     *        the document, asking about unsaved changes first. A no-op in
     *        fixture mode.
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

    /**
     * @brief Loads @p config into the panels as the document.
     * @param[in] keep_baseline  true for Reset to Defaults: the new values
     *                           count as changes against what was loaded.
     */
    void load(const Config &config, Source source, const QString &path,
              bool keep_baseline = false);
    /// What the panels would write, without changing the document.
    bool collect(Config &config) const;
    /// Writes the document to @p path; reports failure in a dialog.
    bool writeFile(const QString &path);
    /// Enables the panels unless the document is a running tag's own.
    void updateEditable();
    /// Re-checks isModified() after a control changes.
    void controlChanged();
    /// Adds @p path to the front of the recent files.
    static void addRecent(const QString &path);

    TagType tag_type_ = TAG_UNSPECIFIED;  ///< Tag type of the document.
    TagState old_state_ = STATE_UNSPECIFIED;  ///< Last state from StateUpdate() or a Start poll.
    Config current_config_;  ///< The document's configuration as last loaded or sent.
    bool fixture_mode_ = false;  ///< Read and Start are no-ops; see SetFixtureMode().
    Source source_ = Source::None;  ///< See source().
    QString path_;               ///< See filePath().
    std::string baseline_;       ///< What the panels wrote just after loading, serialised.
    bool loading_ = false;       ///< Panels are being loaded; ignore their signals.
    bool was_modified_ = false;  ///< isModified() at the last documentChanged().
    bool attached_ = false;      ///< A tag is attached (StateUpdate() seen, no Detach()).
    TagType attached_tag_type_ = TAG_UNSPECIFIED;  ///< See SetAttachedTagType().

    /// Every panel, built once in the constructor, in page order.
    std::vector<ConfigPanel *> panels_;
    /// The panels the current tag type shows, in page order.
    std::vector<ConfigPanel *> shown_;
    /// Shown on the page when there is no document.
    QLabel *placeholder_ = nullptr;

    // for errors

    QMessageBox msgBox;  ///< Reused for "Tag config read failed".
    Tag *tag = nullptr;  ///< Live tag from Attach(); null when detached.
    Ui::ConfigTab ui;    ///< Page, scroll area and file/tag buttons.
    
};

#endif // CONFIG_H
