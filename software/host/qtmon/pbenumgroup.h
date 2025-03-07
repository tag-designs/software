/*
    Helper class for building group boxes from a vector of button definitions.
        buttons are defined by their index (typically chosen to match an enum), and
        label.
*/


#ifndef PB_ENUMGROUP_H
#define PB_ENUMGROUP_H

#include <vector>
#include <QButtonGroup>
#include <QLayout>
#include <QGroupBox>
#include <QVBoxLayout>
#include "tag.pb.h"

using namespace  google::protobuf;

/* namespace Ui
{
    class PBEnumGroup;
}
 */
class PBEnumGroup : public QGroupBox
{
    Q_OBJECT

public:
    struct binfo
    {
        int index;
        const char *label;
        const char *toolTip;
    };
    explicit PBEnumGroup(const char *label,
                         const std::vector<struct binfo> &buttons,
                         QWidget *parent = nullptr);
    ~PBEnumGroup();

    void replaceButtons(const std::vector<struct binfo> &buttons);
    int  checkedId();
    QAbstractButton *button(int id);
    void setCheckedId(int index);

signals:

    void idClicked(int);

private:
    QVBoxLayout *vbox;
    QButtonGroup *buttonGroup;
};

#endif