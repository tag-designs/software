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

class ConfigTab :  public QWidget {
    Q_OBJECT

public:
    explicit ConfigTab(QWidget *parent = nullptr);
    ~ConfigTab();
    bool GetConfig(Config &config);
    bool SetConfig(const Config &config);
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

    bool Attach(Tag &tag);
    void Detach();
    void StateUpdate(TagState state);

private slots:

    void on_configSaveButton_clicked();
    void on_configRestoreButton_clicked();
    void on_startButton_clicked();
    void on_readButton_clicked();

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

    TagType tag_type_ = TAG_UNSPECIFIED;
    TagState old_state_ = STATE_UNSPECIFIED;
    Config current_config_;
    bool fixture_mode_ = false;

    /// Every panel, built once in the constructor, in page order.
    std::vector<ConfigPanel *> panels_;
    /// The panels the current tag type shows, in page order.
    std::vector<ConfigPanel *> shown_;
    bool active = false;

    // for errors

    QMessageBox msgBox;
    Tag *tag;
    Ui::ConfigTab ui;
    
};

#endif // CONFIG_H
