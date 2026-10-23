/**
 * @file    qtuserguide.h
 * @brief   Opens a host application's page of the user manual.
 *
 * @details The user manual (host/docs, built by the `docs` target with MkDocs)
 *          is found wherever it is: packaged beside the application, in the
 *          build tree, or built in the source tree. Every Qt application's
 *          Help menu uses this, so they all look in the same places;
 *          addHelpMenu() builds the usual Help menu for an application that
 *          needs nothing more.
 *          Header-only, like the rest of host_common.
 */

#ifndef QTUSERGUIDE_H
#define QTUSERGUIDE_H

#include <QCoreApplication>
#include <QDesktopServices>
#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QMainWindow>
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

/**
 * @brief   Adds a Help menu with "<app> User Guide" and "About <app>".
 *
 * @details The guide entry calls open() for @p page; the About entry shows
 *          @p about in QMessageBox::about(). The menu bar is put in the window
 *          (QMenuBar::setNativeMenuBar(false)), as in sensorviz and qtmonitor,
 *          so every application's menus are in the same place.
 *
 * @param[in] window  Window whose menu bar receives the menu; also the parent
 *                    of the actions and of the dialogs they open.
 * @param[in] name    Application name as the user sees it, e.g. "Tag Programmer".
 * @param[in] page    The manual page's name under `apps/`, e.g. "qtprogram".
 * @param[in] about   Text of the About box: version and purpose.
 *
 * @return  The Help menu, for an application to add more entries.
 */
inline QMenu *addHelpMenu(QMainWindow *window, const QString &name, const QString &page,
                          const QString &about)
{
  QMenuBar *bar = window->menuBar();
  bar->setNativeMenuBar(false);
  QMenu *help = bar->addMenu(QCoreApplication::translate("HostUserGuide", "&Help"));
  const QString guide =
      QCoreApplication::translate("HostUserGuide", "%1 User Guide").arg(name);
  QAction *guide_action = help->addAction(guide);
  guide_action->setToolTip(
      QCoreApplication::translate("HostUserGuide", "Open the %1 page of the user manual")
          .arg(name));
  QObject::connect(guide_action, &QAction::triggered, window,
                   [window, guide, page]() { open(window, guide, page); });
  const QString about_title =
      QCoreApplication::translate("HostUserGuide", "About %1").arg(name);
  QAction *about_action = help->addAction(about_title);
  QObject::connect(about_action, &QAction::triggered, window,
                   [window, about_title, about]() {
                     QMessageBox::about(window, about_title, about);
                   });
  return help;
}

} // namespace HostUserGuide

#endif // QTUSERGUIDE_H
