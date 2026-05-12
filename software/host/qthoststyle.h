#ifndef HOST_QTHOSTSTYLE_H
#define HOST_QTHOSTSTYLE_H

#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QPalette>
#include <QString>
#include <QStyle>
#include <QStyleFactory>

namespace HostStyle {

inline QPalette lightPalette()
{
  QPalette palette;

  const QColor window(0xec, 0xf0, 0xf4);
  const QColor panel(0xff, 0xff, 0xff);
  const QColor alternate(0xf1, 0xf5, 0xf9);
  const QColor button(0xd2, 0xdc, 0xe7);
  const QColor buttonHover(0xc2, 0xd1, 0xe1);
  Q_UNUSED(buttonHover);
  const QColor border(0x69, 0x7b, 0x8d);
  Q_UNUSED(border);
  const QColor text(0x08, 0x0f, 0x18);
  const QColor mutedText(0x3f, 0x4b, 0x59);
  const QColor accent(0x2f, 0x6f, 0xbd);

  palette.setColor(QPalette::Window, window);
  palette.setColor(QPalette::WindowText, text);
  palette.setColor(QPalette::Base, panel);
  palette.setColor(QPalette::AlternateBase, alternate);
  palette.setColor(QPalette::ToolTipBase, panel);
  palette.setColor(QPalette::ToolTipText, text);
  palette.setColor(QPalette::Text, text);
  palette.setColor(QPalette::Button, button);
  palette.setColor(QPalette::ButtonText, text);
  palette.setColor(QPalette::BrightText, Qt::red);
  palette.setColor(QPalette::Link, accent);
  palette.setColor(QPalette::Highlight, accent);
  palette.setColor(QPalette::HighlightedText, Qt::white);

  palette.setColor(QPalette::Disabled, QPalette::WindowText, mutedText);
  palette.setColor(QPalette::Disabled, QPalette::Text, mutedText);
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, mutedText);
  palette.setColor(QPalette::Disabled, QPalette::Base, QColor(0xe3, 0xe8, 0xee));
  palette.setColor(QPalette::Disabled, QPalette::Button, QColor(0xd6, 0xde, 0xe7));
  palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x9b, 0xb7, 0xd6));

  return palette;
}

inline QString lightStyleSheet()
{
  return QStringLiteral(R"(
QWidget {
  color: #080f18;
  selection-background-color: #2f6fbd;
  selection-color: #ffffff;
}

QMainWindow,
QDialog,
QWidget#centralWidget {
  background: #ecf0f4;
}

QGroupBox {
  background: #ffffff;
  border: 1px solid #8b9bab;
  border-radius: 4px;
  margin-top: 1.25em;
  padding: 8px;
}

QGroupBox::title {
  subcontrol-origin: margin;
  left: 8px;
  padding: 0 4px;
  color: #080f18;
}

QTabWidget::pane {
  border: 1px solid #7c8d9e;
  background: #ffffff;
}

QTabBar::tab {
  background: #d2dce7;
  color: #080f18;
  border: 1px solid #7c8d9e;
  border-bottom-color: #6d7f91;
  padding: 5px 10px;
}

QTabBar::tab:selected {
  background: #ffffff;
  border-bottom-color: #ffffff;
}

QLineEdit,
QTextEdit,
QPlainTextEdit,
QComboBox,
QSpinBox,
QDoubleSpinBox,
QDateEdit,
QTimeEdit,
QDateTimeEdit {
  background: #ffffff;
  color: #080f18;
  border: 1px solid #697b8d;
  border-radius: 3px;
  padding: 2px 4px;
}

QLineEdit:focus,
QTextEdit:focus,
QPlainTextEdit:focus,
QComboBox:focus,
QSpinBox:focus,
QDoubleSpinBox:focus,
QDateEdit:focus,
QTimeEdit:focus,
QDateTimeEdit:focus {
  border-color: #2f6fbd;
}

QLineEdit:disabled,
QTextEdit:disabled,
QPlainTextEdit:disabled,
QComboBox:disabled,
QSpinBox:disabled,
QDoubleSpinBox:disabled,
QDateEdit:disabled,
QTimeEdit:disabled,
QDateTimeEdit:disabled {
  background: #e3e8ee;
  color: #3f4b59;
  border-color: #9aa8b6;
}

QPushButton {
  background: #d2dce7;
  color: #080f18;
  border: 1px solid #5f7184;
  border-radius: 4px;
  padding: 4px 10px;
}

QPushButton:hover {
  background: #c2d1e1;
}

QPushButton:pressed {
  background: #aebfd2;
}

QPushButton:disabled {
  background: #d6dee7;
  color: #3f4b59;
  border-color: #9aa8b6;
}

QComboBox::drop-down {
  background: #c2d1e1;
  border-left: 1px solid #5f7184;
  width: 18px;
}

QComboBox::drop-down:hover {
  background: #aebfd2;
}

QComboBox::drop-down:disabled {
  background: #cbd4df;
  border-left-color: #9aa8b6;
}

QCheckBox,
QRadioButton,
QLabel {
  color: #080f18;
}

QCheckBox:disabled,
QRadioButton:disabled,
QLabel:disabled {
  color: #3f4b59;
}

QMenu,
QMenuBar {
  background: #ffffff;
  color: #080f18;
}

QMenu::item:selected {
  background: #2f6fbd;
  color: #ffffff;
}

QHeaderView::section {
  background: #d2dce7;
  color: #080f18;
  border: 1px solid #7c8d9e;
  padding: 4px;
}

QTableView,
QTreeView,
QListView {
  background: #ffffff;
  alternate-background-color: #f1f5f9;
  color: #080f18;
  gridline-color: #aeb9c5;
}

QProgressBar {
  background: #ffffff;
  color: #080f18;
  border: 1px solid #697b8d;
  border-radius: 3px;
  text-align: center;
}

QProgressBar::chunk {
  background: #2f6fbd;
}

QToolTip {
  background: #ffffff;
  color: #080f18;
  border: 1px solid #697b8d;
}
)");
}

inline void apply(QApplication &app)
{
  const QString mode = QString::fromLocal8Bit(qgetenv("TAG_HOST_STYLE"))
                           .trimmed()
                           .toLower();

  if (mode == QStringLiteral("system") || mode == QStringLiteral("native") ||
      mode == QStringLiteral("none"))
    return;

#ifdef Q_OS_MACOS
  if (mode.isEmpty())
    return;
#endif

#ifdef Q_OS_WIN
  if (mode.isEmpty()) {
    QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion"));
    if (fusion != nullptr)
      app.setStyle(fusion);
    return;
  }
#endif

  QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion"));
  if (fusion != nullptr)
    app.setStyle(fusion);
  app.setPalette(lightPalette());
  app.setStyleSheet(lightStyleSheet());
}

} // namespace HostStyle

#endif // HOST_QTHOSTSTYLE_H
