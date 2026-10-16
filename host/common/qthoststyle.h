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

/**
 * @brief   The palette of the host applications' light style.
 *
 * @details Neutral greys modelled on the macOS native style, which is what the
 *          applications look like on the Mac: a grey window, panels a step
 *          darker, white fields. Every shading role (Light to Shadow) is set,
 *          because Fusion draws frames, separators and sunken edges from them
 *          and derives near-white defaults otherwise.
 */
inline QPalette lightPalette()
{
  QPalette palette;

  const QColor window(0xec, 0xec, 0xec);
  const QColor panel(0xff, 0xff, 0xff);
  const QColor alternate(0xf4, 0xf4, 0xf4);
  const QColor button(0xff, 0xff, 0xff);
  const QColor text(0x1a, 0x1a, 0x1a);
  const QColor mutedText(0x8a, 0x8a, 0x8a);
  const QColor accent(0x1f, 0x6f, 0xd0);

  palette.setColor(QPalette::Window, window);
  palette.setColor(QPalette::WindowText, text);
  palette.setColor(QPalette::Base, panel);
  palette.setColor(QPalette::AlternateBase, alternate);
  palette.setColor(QPalette::ToolTipBase, panel);
  palette.setColor(QPalette::ToolTipText, text);
  palette.setColor(QPalette::PlaceholderText, mutedText);
  palette.setColor(QPalette::Text, text);
  palette.setColor(QPalette::Button, button);
  palette.setColor(QPalette::ButtonText, text);
  palette.setColor(QPalette::BrightText, Qt::red);
  palette.setColor(QPalette::Link, accent);
  palette.setColor(QPalette::Highlight, accent);
  palette.setColor(QPalette::HighlightedText, Qt::white);

  palette.setColor(QPalette::Light, QColor(0xff, 0xff, 0xff));
  palette.setColor(QPalette::Midlight, QColor(0xe4, 0xe4, 0xe4));
  palette.setColor(QPalette::Mid, QColor(0xb4, 0xb4, 0xb4));
  palette.setColor(QPalette::Dark, QColor(0x9a, 0x9a, 0x9a));
  palette.setColor(QPalette::Shadow, QColor(0x6e, 0x6e, 0x6e));

  palette.setColor(QPalette::Disabled, QPalette::WindowText, mutedText);
  palette.setColor(QPalette::Disabled, QPalette::Text, mutedText);
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, mutedText);
  palette.setColor(QPalette::Disabled, QPalette::Base, QColor(0xf2, 0xf2, 0xf2));
  palette.setColor(QPalette::Disabled, QPalette::Button, QColor(0xeb, 0xeb, 0xeb));
  palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x9d, 0xb8, 0xdc));

  return palette;
}

/**
 * @brief   The style sheet of the host applications' light style.
 *
 * @details Panels step darker as they nest -- tab pane, group box, group box
 *          within a group box -- as the macOS native style does, so a page's
 *          structure shows without heavy borders. Fields and buttons are white
 *          with a visible border.
 */
inline QString lightStyleSheet()
{
  return QStringLiteral(R"(
QWidget {
  color: #1a1a1a;
  selection-background-color: #1f6fd0;
  selection-color: #ffffff;
}

QMainWindow,
QDialog,
QWidget#centralWidget {
  background: #ececec;
}

QTabWidget::pane {
  border: 1px solid #b4b4b4;
  border-radius: 4px;
  background: #e5e5e5;
}

QTabBar::tab {
  background: #d9d9d9;
  color: #1a1a1a;
  border: 1px solid #b4b4b4;
  padding: 4px 12px;
}

QTabBar::tab:selected {
  background: #e5e5e5;
  border-bottom-color: #e5e5e5;
}

QTabBar::tab:hover:!selected {
  background: #cfcfcf;
}

QGroupBox {
  background: #dedede;
  border: 1px solid #b4b4b4;
  border-radius: 4px;
  margin-top: 1.25em;
  padding: 8px;
}

QGroupBox QGroupBox {
  background: #d4d4d4;
  border-color: #aaaaaa;
}

QGroupBox::title {
  subcontrol-origin: margin;
  left: 8px;
  padding: 0 4px;
  color: #1a1a1a;
}

QGroupBox::title:disabled {
  color: #8a8a8a;
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
  color: #1a1a1a;
  border: 1px solid #a8a8a8;
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
  border-color: #1f6fd0;
}

QLineEdit:read-only {
  background: #f6f6f6;
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
  background: #ebebeb;
  color: #8a8a8a;
  border-color: #c4c4c4;
}

QPushButton {
  background: #ffffff;
  color: #1a1a1a;
  border: 1px solid #9c9c9c;
  border-radius: 4px;
  padding: 4px 12px;
}

QPushButton:hover {
  background: #f2f2f2;
}

QPushButton:pressed {
  background: #d6d6d6;
}

QPushButton:default {
  border-color: #1f6fd0;
}

QPushButton:disabled {
  background: #ebebeb;
  color: #8a8a8a;
  border-color: #c4c4c4;
}

QComboBox::drop-down {
  background: #e4e4e4;
  border-left: 1px solid #a8a8a8;
  width: 18px;
}

QComboBox::drop-down:hover {
  background: #d6d6d6;
}

QComboBox::drop-down:disabled {
  background: #e4e4e4;
  border-left-color: #c4c4c4;
}

QCheckBox,
QRadioButton,
QLabel {
  color: #1a1a1a;
}

QCheckBox:disabled,
QRadioButton:disabled,
QLabel:disabled {
  color: #8a8a8a;
}

QMenuBar {
  background: #e4e4e4;
  color: #1a1a1a;
  border-bottom: 1px solid #c4c4c4;
}

QMenuBar::item:selected {
  background: #d0d0d0;
}

QMenu {
  background: #f6f6f6;
  color: #1a1a1a;
  border: 1px solid #b4b4b4;
}

QMenu::item:selected {
  background: #1f6fd0;
  color: #ffffff;
}

QMenu::item:disabled {
  color: #8a8a8a;
}

QHeaderView::section {
  background: #e0e0e0;
  color: #1a1a1a;
  border: 1px solid #b4b4b4;
  padding: 4px;
}

QTableView,
QTreeView,
QListView {
  background: #ffffff;
  alternate-background-color: #f4f4f4;
  color: #1a1a1a;
  gridline-color: #c8c8c8;
  border: 1px solid #b4b4b4;
}

QProgressBar {
  background: #ffffff;
  color: #1a1a1a;
  border: 1px solid #a8a8a8;
  border-radius: 3px;
  text-align: center;
}

QProgressBar::chunk {
  background: #1f6fd0;
}

QToolTip {
  background: #ffffff;
  color: #1a1a1a;
  border: 1px solid #a8a8a8;
}
)");
}

/**
 * @brief   Applies the host applications' style to @p app.
 *
 * @details On Windows and Linux the applications use Fusion with the light
 *          palette and style sheet above. Fusion's own defaults, with the
 *          system's light palette, are near-white throughout, which loses the
 *          panel structure the macOS native style shows. macOS keeps its native
 *          style.
 *
 *          The TAG_HOST_STYLE environment variable overrides the default:
 *          "system", "native" or "none" leaves Qt's platform style alone on
 *          every platform; any other value applies the light style, on macOS
 *          too, which is how it is previewed there.
 *
 * @param[in,out] app  The application; call before any window is created.
 */
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

  QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion"));
  if (fusion != nullptr)
    app.setStyle(fusion);
  app.setPalette(lightPalette());
  app.setStyleSheet(lightStyleSheet());
}

} // namespace HostStyle

#endif // HOST_QTHOSTSTYLE_H
