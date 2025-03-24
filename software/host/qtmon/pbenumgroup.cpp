#include <QDebug>
#include <QGroupBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QLayout>

#include "pbenumgroup.h"

PBEnumGroup::PBEnumGroup(const char *title,
                         const std::vector<struct binfo> &buttons,
                         QWidget *parent) : QGroupBox(title, parent)
{
    vbox = new QVBoxLayout;
    buttonGroup = new QButtonGroup(this);

    setLayout(vbox);

    assert(connect(buttonGroup,&QButtonGroup::buttonClicked,
        this, &PBEnumGroup::buttonClicked));


    replaceButtons(buttons);
}

PBEnumGroup::~PBEnumGroup()
{
}

int PBEnumGroup::checkedId()
{
    return buttonGroup->checkedId();
}

QAbstractButton *PBEnumGroup::button(int id)
{
    return buttonGroup->button(id);
}

void PBEnumGroup::setCheckedId(int index)
{
    QAbstractButton *button = buttonGroup->button(index);
    if (button)
        button->setChecked(true);
}

void PBEnumGroup::replaceButtons(const std::vector<struct binfo> &buttons)
{
    bool first = true;
    for (std::size_t i = 0; i < buttons.size(); i++)
    {
        QRadioButton *button = new QRadioButton(buttons[i].label);
        if (first)
        {
            button->setChecked(true);
            first = false;
        }
        buttonGroup->addButton(button, buttons[i].index);
        if (buttons[i].toolTip != nullptr)
        {
            button->setToolTip(buttons[i].toolTip);
        }
        vbox->addWidget(button);
    }
    vbox->addStretch(1);
}

void PBEnumGroup::buttonClicked(QAbstractButton *button){
    int id = buttonGroup->id(button);
    emit idClicked(id);
}
