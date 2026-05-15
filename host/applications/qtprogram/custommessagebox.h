
#ifndef CUSTOMMESSAGEBOX_H
#define CUSTOMMESSAGEBOX_H

#include <QDialog>
#include <QPushButton>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QCloseEvent>
#include <QLabel>
#include <QProgressBar>

class CustomMessageBox : public QDialog {
public:
    CustomMessageBox(const QString& text, QWidget *parent = nullptr);

    Q_OBJECT


signals:

    void OkButtonClicked();

public slots:

    void setText(QString text);
    void setMaximum(int max);
    void setValue(int value);

private slots:
    
    void onCancelButtonClicked();
    void onOkButtonClicked();
    void closeEvent(QCloseEvent *event) override;

private:
    QLabel* messageLabel;
    QPushButton* okButton;
    QPushButton* cancelButton;
    QProgressBar* progressBar;
    bool closeButtonClicked = false;
};

#endif