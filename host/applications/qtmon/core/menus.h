/**
 * @file    menus.h
 * @brief   qtmonitor's menus as data: what each command is called, its
 *          tooltip and shortcut, and where it appears.
 *
 * @details The window builds its actions, menu bar and popup menus from
 *          these tables in one loop, so the tables are the whole description
 *          of the menus; any front end can be built from them. Which entries
 *          are enabled is a separate rule, availability() in commands.h.
 *          `qtmonitor --check-menus` prints the menus the window built, and is
 *          compared with test/menus.txt. See
 *          host/applications/qtmon/design/architecture.md.
 */

#ifndef QTMON_CORE_MENUS_H
#define QTMON_CORE_MENUS_H

#include <vector>

#include "commands.h"

namespace qtmon {

/// A standard shortcut, which each platform maps to its own keys.
enum class Shortcut {
  None,    ///< No shortcut.
  Open,    ///< Ctrl+O / Cmd+O.
  Save,    ///< Ctrl+S / Cmd+S.
  Quit,    ///< The platform's quit key.
};

/// How an entry is labelled.
struct EntryText {
  const char *label;    ///< The menu text; "..." marks an entry that asks for more.
  const char *tooltip;  ///< Hover text; empty for none.
  Shortcut shortcut;    ///< Its shortcut, if any.
};

/// The text, tooltip and shortcut of @p command.
EntryText commandText(Command command);

/**
 * @brief Menu entries that are not tag or document commands: they are
 *        always available, so availability() does not cover them.
 */
enum class MenuItem {
  NewConfiguration,  ///< Submenu: one entry per tag type with a default configuration.
  OpenRecent,        ///< Submenu: the recent configuration files, filled when it opens.
  SaveErrorLog,      ///< The Error Log tab's Save Error Log….
  ClearErrorLog,     ///< The Error Log tab's Clear Error Log.
  LogLevel,          ///< Submenu: the Error Log tab's level, TRACE to ERROR.
  Quit,              ///< Closes the window, which may ask to save.
  UserGuide,         ///< Opens qtmonitor's page of the user manual.
  About,             ///< Version and purpose.
};

/**
 * @brief The text, tooltip and shortcut of @p item.
 * @details SaveErrorLog, ClearErrorLog and LogLevel are the Error Log tab's
 *          own actions, which carry their own text; the text here describes
 *          them for a front end that builds them itself.
 */
EntryText itemText(MenuItem item);

/// One entry in a menu.
struct MenuEntry {
  /// What the entry is.
  enum class Kind {
    Command,    ///< A Command; see commandText() and availability().
    Item,       ///< A MenuItem; see itemText().
    Separator,  ///< A dividing line.
  };
  Kind kind;                                ///< See Kind.
  Command command = Command::Attach;        ///< For Kind::Command.
  MenuItem item = MenuItem::Quit;           ///< For Kind::Item.
};

/// A menu in the menu bar.
struct Menu {
  const char *title;               ///< With its mnemonic, e.g. "&File".
  std::vector<MenuEntry> entries;  ///< In order.
};

/// The menu bar: File, Tag, Log and Help, in order.
const std::vector<Menu> &menuBar();

/// The popup menu of the Tag page and of the status strip: the tag commands.
const std::vector<MenuEntry> &tagPopup();

/// The popup menu of the Configuration tab: its document commands and Start.
const std::vector<MenuEntry> &configPopup();

} // namespace qtmon

#endif // QTMON_CORE_MENUS_H
