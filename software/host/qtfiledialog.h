#ifndef HOST_QTFILEDIALOG_H
#define HOST_QTFILEDIALOG_H

#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QWidget>

namespace HostFileDialog {

inline void setMacDialogOptions(QFileDialog &dialog)
{
#ifdef Q_OS_MACOS
  dialog.setOption(QFileDialog::DontUseNativeDialog, true);
#else
  Q_UNUSED(dialog);
#endif
}

inline QString getOpenFileName(QWidget *parent, const QString &caption,
                               const QString &directory,
                               const QString &filter = QString())
{
  QFileDialog dialog(parent);
  setMacDialogOptions(dialog);
  dialog.setWindowTitle(caption);
  dialog.setDirectory(directory);
  if (!filter.isEmpty())
    dialog.setNameFilter(filter);
  dialog.setAcceptMode(QFileDialog::AcceptOpen);
  dialog.setFileMode(QFileDialog::ExistingFile);
  if (dialog.exec() != QDialog::Accepted)
    return QString();
  return dialog.selectedFiles().value(0);
}

inline QString getSaveFileName(QWidget *parent, const QString &caption,
                               const QString &directory,
                               const QString &filter = QString(),
                               QString *selectedFilter = nullptr)
{
  QFileDialog dialog(parent);
  setMacDialogOptions(dialog);
  dialog.setWindowTitle(caption);
  const QFileInfo initialPath(directory);
  if (initialPath.exists() && initialPath.isDir()) {
    dialog.setDirectory(directory);
  } else {
    dialog.setDirectory(initialPath.path());
    dialog.selectFile(initialPath.fileName());
  }
  if (!filter.isEmpty())
    dialog.setNameFilter(filter);
  dialog.setAcceptMode(QFileDialog::AcceptSave);
  dialog.setFileMode(QFileDialog::AnyFile);
  if (dialog.exec() != QDialog::Accepted)
    return QString();
  if (selectedFilter)
    *selectedFilter = dialog.selectedNameFilter();
  return dialog.selectedFiles().value(0);
}

} // namespace HostFileDialog

#endif // HOST_QTFILEDIALOG_H
