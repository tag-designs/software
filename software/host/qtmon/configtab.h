#ifndef CONFIG_H
#define CONFIG_H

#include <QWidget>
//#include <QDateTime>
#include <QTabWidget>
#include <QPushButton>
#include <QList>
#include "tag.pb.h"
//#include "host.pb.h"
//#include "tagclass.h"
#include "sensors.h"
#include "schedule.h"
#include "dataconfig.h"

/* namespace Ui
{
    class ConfigTab;
}
 */
class ConfigTab : public QWidget
{
    Q_OBJECT

public:
    explicit ConfigTab(QWidget *parent = nullptr);
    ~ConfigTab();
    bool GetConfig(Config &config);
    void SetConfig(const Config &config);

signals:

    void start_clicked();
    void config_restore_clicked();

public slots:

    void Attach(const Config &config);
    void Detach();
    void StateUpdate(TagState state);

private slots:

    void on_saveButton_clicked();       // save configuration to file
    void on_restoreButton_clicked();    // restore configuration from file

private:

    // Helper function

    TagType tag_type_ = TAG_UNSPECIFIED;

    void AddConfigItem(ConfigInterface *, const char *title, const char *tip, const Config &config);

    // List of configuration tabs

    QList<ConfigInterface *> configlist_;
    TagState old_state_ = STATE_UNSPECIFIED;

    // User interface 

    QTabWidget *tabwidget_ = nullptr;
    QPushButton *savebtn_ = nullptr;
    QPushButton *restorebtn_ = nullptr;
    QPushButton *readbtn_ = nullptr;
    QPushButton *startbtn_ = nullptr;
};

#endif // CONFIG_H
