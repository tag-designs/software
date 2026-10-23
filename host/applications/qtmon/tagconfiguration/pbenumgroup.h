/**
 * @file    pbenumgroup.h
 * @brief   A group box of radio buttons, one per value of a protobuf enum.
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
/**
 * @class   PBEnumGroup
 * @brief   Chooses one value of an enum with radio buttons whose ids are the
 *          enum's numbers.
 *
 * @details The configuration panels read a field with setCheckedId() and
 *          write it from checkedId(). The first button is checked when the
 *          group is built, and setCheckedId() with a value that has no button
 *          changes nothing, so the group then still shows (and checkedId()
 *          returns) the first button, or whichever was checked last.
 */
class PBEnumGroup : public QGroupBox
{
    Q_OBJECT

public:
    /// One button: its enum number, label and optional tooltip.
    struct binfo
    {
        int index;            ///< Enum number; the button's id.
        const char *label;    ///< Button text.
        const char *toolTip;  ///< Tooltip, or nullptr for none.
    };

    /**
     * @param[in] label    Group box title.
     * @param[in] buttons  Buttons in display order; the first is checked.
     * @param[in] parent   Qt parent.
     */
    explicit PBEnumGroup(const char *label,
                         const std::vector<struct binfo> &buttons,
                         QWidget *parent = nullptr);
    ~PBEnumGroup();

    /**
     * @brief Adds @p buttons after any existing ones and checks the first of
     *        them.
     * @note  Despite its name, it removes nothing; the constructor uses it.
     */
    void replaceButtons(const std::vector<struct binfo> &buttons);
    /// Id of the checked button: an enum number, or -1 if none is checked.
    int  checkedId();
    /// The button with id @p id, or nullptr if there is none.
    QAbstractButton *button(int id);
    /// Checks the button with id @p index; does nothing if there is none.
    void setCheckedId(int index);

private:
    /// Re-emits a click as idClicked() with the button's id.
    void buttonClicked(QAbstractButton *button);

signals:

    /// A button was clicked by the user; the argument is its enum number.
    void idClicked(int);

private:
    QVBoxLayout *vbox;          ///< Holds the buttons and a trailing stretch.
    QButtonGroup *buttonGroup;  ///< Exclusive group; ids are enum numbers.
};

#endif