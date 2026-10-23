/**
 * @file    qtuserguide.h
 * @brief   Opens a host application's page of the user manual.
 *
 * @details The user manual (host/docs, built by the `docs` target with MkDocs)
 *          is found wherever it is: packaged beside the application, in the
 *          build tree, or built in the source tree. Every Qt application's
 *          Help menu uses this, so they all look in the same places.
 *          Header-only, like the rest of host_common.
 */

#ifndef QTUSERGUIDE_H
#define QTUSERGUIDE_H

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QWidget>

namespace HostUserGuide
{

/**
 * @brief   Directories that may hold the built user manual, in search order.
 *
 * @details From the application's directory and up to five levels above it:
 *          `docs`, `host/docs/site` and `share/UltralightTags/docs`. This
 *          covers packaged layouts and running from a build tree. Then the
 *          build tree's site (`TAG_DESIGNS_DOCS_BUILD_DIR`) and the source
 *          tree's (`TAG_DESIGNS_DOCS_SOURCE_DIR`/site), which host_common
 *          defines for every application. Duplicates are removed.
 */
inline QStringList documentationRoots()
{
  QStringList roots;
  QDir probe_dir(QCoreApplication::applicationDirPath());

  for (int level = 0; level < 6; ++level) {
    roots << probe_dir.filePath(QStringLiteral("docs"));
    roots << probe_dir.filePath(QStringLiteral("host/docs/site"));
    roots << probe_dir.filePath(QStringLiteral("share/UltralightTags/docs"));
    if (!probe_dir.cdUp())
      break;
  }

#ifdef TAG_DESIGNS_DOCS_BUILD_DIR
  roots << QStringLiteral(TAG_DESIGNS_DOCS_BUILD_DIR);
#endif
#ifdef TAG_DESIGNS_DOCS_SOURCE_DIR
  roots << QDir(QStringLiteral(TAG_DESIGNS_DOCS_SOURCE_DIR)).filePath(QStringLiteral("site"));
#endif

  QStringList unique_roots;
  for (const QString &root : roots) {
    const QString clean_root = QDir::cleanPath(root);
    if (!clean_root.isEmpty() && clean_root != QStringLiteral(".") &&
        !unique_roots.contains(clean_root))
      unique_roots << clean_root;
  }
  return unique_roots;
}

/**
 * @brief   Opens an application's user manual page in the default browser.
 *
 * @param[in] parent  Parent for the "not found" message.
 * @param[in] title   Guide title for that message, e.g. "Tag Monitor User Guide".
 * @param[in] app     The page's name under `apps/`, e.g. "qtmonitor". Both
 *                    `apps/<app>.html` and `apps/<app>/index.html` are tried.
 *
 * @return  true when a page was found and handed to the browser; false when
 *          none was found, after telling the user how to build the manual and
 *          where it looked.
 */
inline bool open(QWidget *parent, const QString &title, const QString &app)
{
  const QStringList pages = {QStringLiteral("apps/%1.html").arg(app),
                             QStringLiteral("apps/%1/index.html").arg(app)};
  const QStringList roots = documentationRoots();

  for (const QString &root : roots) {
    for (const QString &page : pages) {
      const QString path = QDir(root).filePath(page);
      if (QFileInfo::exists(path)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        return true;
      }
    }
  }

  QMessageBox message(parent);
  message.setIcon(QMessageBox::Information);
  message.setWindowTitle(title);
  message.setText(QCoreApplication::translate("HostUserGuide", "The %1 was not found.")
                      .arg(title));
  message.setInformativeText(QCoreApplication::translate(
      "HostUserGuide", "Build the host documentation target and try again:\n"
                       "cmake --build <build-dir> --target docs"));
  message.setDetailedText(QCoreApplication::translate("HostUserGuide", "Looked in:\n%1")
                              .arg(roots.join(QStringLiteral("\n"))));
  message.exec();
  return false;
}

} // namespace HostUserGuide

#endif // QTUSERGUIDE_H
