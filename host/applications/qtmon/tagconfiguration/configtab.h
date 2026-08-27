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
#include "schedule.h"
#include "adxl362config.h"
#include "lsm6dsvconfig.h"
#include "bittaglog.h"
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
     * @brief Reports whether the current tag exposes any sensor controls.
     *
     * @return true when at least one sensor configuration module is active;
     *         false when the Sensors sub-tab should be hidden or skipped.
     */
    bool HasSensorConfiguration();
    /**
     * @brief Selects the inner configuration sub-tab for screenshot capture.
     */
    void ShowScheduleTab();
    /**
     * @brief Selects the inner sensor sub-tab for screenshot capture.
     */
    void ShowSensorTab();
    /**
     * @brief Puts the configuration tab in read-only fixture display mode.
     *
     * @details Fixture mode is used by qtmonitor documentation screenshots.
     *          It allows SetConfig() and StateUpdate() to populate and enable
     *          the real tag-specific widgets while blocking actions that would
     *          require a live Tag object, such as Read and Start.
     */
    void SetFixtureMode(bool enabled);

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
     * @brief Shows or hides the Sensors tab to match active sensor modules.
     *
     * @details SetConfig() updates child module activity first, then calls this
     *          helper so tags without user-configurable sensors do not present
     *          an empty Sensors tab in normal UI or screenshots.
     */
    void UpdateSensorTabVisibility();

    // Helper function

    TagType tag_type_ = TAG_UNSPECIFIED;
    TagState old_state_ = STATE_UNSPECIFIED;
    Config current_config_;
    bool fixture_mode_ = false;

    // Schedule tab and components

    //QWidget scheduleTab;

    Schedule schedule;
    BitTagLogTab btlog;

    // Sensor tab and components

    //QWidget sensorTab;

    Adxl362Config adxl;
    Lsm6dsvConfig lsm;
    bool active = false;

    // for errors

    QMessageBox msgBox;
    Tag *tag;
    Ui::ConfigTab ui;
    
};

#endif // CONFIG_H
