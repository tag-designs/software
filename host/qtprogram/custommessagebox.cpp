
#include "custommessagebox.h"

CustomMessageBox::CustomMessageBox(const QString& text, QWidget *parent) : QDialog(parent) {
    setWindowTitle("Custom Message");

    messageLabel = new QLabel(text);
    okButton = new QPushButton("OK");
    cancelButton = new QPushButton("Cancel");
    progressBar = new QProgressBar();

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(progressBar);
    layout->addWidget(messageLabel);
    layout->addWidget(okButton);
    layout->addWidget(cancelButton);

    progressBar->setVisible(false);
    connect(okButton, &QPushButton::clicked, this, &CustomMessageBox::onOkButtonClicked);
    connect(cancelButton, &QPushButton::clicked, this, &CustomMessageBox::onCancelButtonClicked);
}

void CustomMessageBox::setText(QString text){
    messageLabel->setText(text);
}

void CustomMessageBox::closeEvent(QCloseEvent *event) {
    if (!closeButtonClicked) {
            event->ignore(); // Ignore close event if not triggered by a closing button
    } else {
        QDialog::closeEvent(event); // Allow close event if triggered by a closing button
    }
}

void CustomMessageBox::onOkButtonClicked() {
    cancelButton->setEnabled(false);
    okButton->setEnabled(false);
    closeButtonClicked = false; 
    emit OkButtonClicked(); // Prevent closing
}

void CustomMessageBox::onCancelButtonClicked() {
    closeButtonClicked = true; // Allow closing
    close();
}

void CustomMessageBox::setMaximum(int max) {
    if (max > 0) {
        progressBar->setMaximum(max);
        
    }
}

void CustomMessageBox::setValue(int value) {
    int max = progressBar->maximum();
    if (value < max) {
        progressBar->setValue(value);
        progressBar->setVisible(true);
    } else {
        progressBar->setVisible(false);
    }
}

