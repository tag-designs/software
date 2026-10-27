/**
 * @file    configtab.h
 * @brief   qtmonitor's Configuration tab: a configuration editor holding one
 *          document (from a tag type's defaults, a file, or a tag), shown as a
 *          page of fixed panels, with new, open, save and reset.
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

#include "configdocument.h"
#include "configpanel.h"
#include "ui_configtab.h"
#include "tag.pb.h"

/**
 * @class   ConfigTab
 * @brief   The configuration editor: one configuration document, shown as a
 *          page of fixed panels, edited and saved.
 *
 * @details The tab holds at most one **document** (qtmon::ConfigDocument,
 *          core/configdocument.h): a Config, its tag type (which selects the
 *          panels, `panelsForTag()` in configtab.cpp), its source (a tag
 *          type's defaults, a file, or a tag), for a file its path, and the
 *          user's approval for Start. The tab shows it and makes the
 *          dialogs. Every ConfigPanel is built once, in the constructor.
 *          Loading a document shows the panels its tag type selects and loads
 *          each from it; GetConfig() starts from the document and lets each
 *          shown panel write the fields it owns, so fields no panel owns pass
 *          through unchanged. Save and Start both use GetConfig().
 *
 *          Unsaved changes are detected by comparing what the panels would
 *          write with what they wrote just after loading; any control's change
 *          re-checks, and documentChanged() reports a change of state.
 *
 *          The tab never talks to a tag. The window reads a tag's
 *          configuration into it (SetConfig()) and sends what GetConfig()
 *          returns to Start, through the tag session. It tells the tab the
 *          tag's state (StateUpdate(), Detach()) only so that editing can be
 *          disabled while the document is the attached tag's own
 *          configuration and the tag is not IDLE: a running tag's
 *          configuration is shown, not edited. The configuration probe uses
 *          the tab with no window at all.
 *
 *          Not thread-safe: use from the GUI thread only.
 */
class ConfigTab :  public QWidget {
    Q_OBJECT

public:
    /// Where the document came from; see qtmon::DocumentSource.
    using Source = qtmon::DocumentSource;

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
    bool hasDocument() const { return doc_.hasDocument(); }
    /// The document's source.
    Source source() const { return doc_.source(); }
    /// The document's tag type; TAG_UNSPECIFIED with no document.
    TagType documentTagType() const { return doc_.tagType(); }
    /// The document's file, for source File; empty otherwise.
    QString filePath() const { return QString::fromStdString(doc_.path()); }

    /// Whether the user has approved the document's values for Start.
    bool isApproved() const { return doc_.approved(); }
    /**
     * @brief Records the user's approval (the status strip's Approved box).
     * @details Ignored with no document. Any later change to the values
     *          withdraws it; see contentChanged().
     */
    void setApproved(bool approved) { doc_.setApproved(approved); }
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
     *        which changes no values. The document's approval has already
     *        been withdrawn when this is emitted.
     */
    void contentChanged();

public slots:

    /**
     * @brief The tag was detached. The document stays, and becomes editable
     *        if it was a running tag's configuration.
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

private:

    /// Sizes the scroll area to the shown panels; see the definition.
    void FitPanelPage();
    /// Height left for the main window's tabs, buttons and frame (pixels).
    static constexpr int kWindowChrome = 260;

    /// Withdraws approval and emits contentChanged().
    void changeContent();

    /**
     * @brief Loads @p config into the panels as the document.
     * @param[in] config         The configuration; its tag_type chooses the panels.
     * @param[in] source         Where it came from, shown in the title.
     * @param[in] path           Its file, for Source::File; otherwise empty.
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

    qtmon::ConfigDocument doc_;  ///< The document: values, source, file, baseline, approval.
    TagState old_state_ = STATE_UNSPECIFIED;  ///< Last state from StateUpdate().
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

    Ui::ConfigTab ui;    ///< Page and scroll area.
    
};

#endif // CONFIG_H
