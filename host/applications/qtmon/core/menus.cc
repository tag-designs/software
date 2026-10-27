/**
 * @file    menus.cc
 * @brief   The menu tables declared in menus.h.
 */

#include "menus.h"

namespace qtmon {

namespace {

/// An entry for @p command.
MenuEntry cmd(Command command)
{
  return {MenuEntry::Kind::Command, command};
}

/// An entry for @p item.
MenuEntry item(MenuItem item)
{
  return {MenuEntry::Kind::Item, Command::Attach, item};
}

/// A separator.
MenuEntry separator()
{
  return {MenuEntry::Kind::Separator};
}

} // namespace

EntryText commandText(Command command)
{
  switch (command) {
  case Command::Attach:
    return {"Attach", "Find a tag base and attach to its tag", Shortcut::None};
  case Command::Detach:
    return {"Detach", "Detach from the tag", Shortcut::None};
  case Command::Read:
    return {"Read Configuration", "Read the tag's configuration", Shortcut::None};
  case Command::Start:
    return {"Start", "Write the configuration to the tag and start it", Shortcut::None};
  case Command::Stop:
    return {"Stop", "Stop the tag", Shortcut::None};
  case Command::Sync:
    return {"Synchronize Clock", "Set the tag's clock from this computer's", Shortcut::None};
  case Command::Test:
    return {"Self-Test", "Run the tag's self-tests", Shortcut::None};
  case Command::Erase:
    return {"Erase...", "Erase the tag's data and return it to IDLE", Shortcut::None};
  case Command::Download:
    return {"Download Data...", "Download the tag's data log to a file", Shortcut::None};
  case Command::Open:
    return {"Open Configuration...", "Open a configuration file for editing", Shortcut::Open};
  case Command::Save:
    return {"Save Configuration", "Save the configuration to its file", Shortcut::Save};
  case Command::SaveAs:
    return {"Save Configuration As...", "Save the configuration to a new file",
            Shortcut::SaveAs};
  case Command::Reset:
    return {"Reset to Defaults",
            "Replace the configuration's values with its tag type's defaults", Shortcut::None};
  }
  return {"", "", Shortcut::None};
}

EntryText itemText(MenuItem item)
{
  switch (item) {
  case MenuItem::NewConfiguration:
    return {"New Configuration", "", Shortcut::None};
  case MenuItem::OpenRecent:
    return {"Open Recent", "", Shortcut::None};
  case MenuItem::SaveErrorLog:
    return {"Save Error Log...", "Save the error log to a text file", Shortcut::None};
  case MenuItem::ClearErrorLog:
    return {"Clear Error Log", "Clear the error log", Shortcut::None};
  case MenuItem::LogLevel:
    return {"Log Level", "", Shortcut::None};
  case MenuItem::Quit:
    return {"Quit", "", Shortcut::Quit};
  case MenuItem::UserGuide:
    return {"Tag Monitor User Guide", "Open the Tag Monitor page of the user manual",
            Shortcut::None};
  case MenuItem::About:
    return {"About Tag Monitor", "Version and purpose", Shortcut::None};
  }
  return {"", "", Shortcut::None};
}

const std::vector<Menu> &menuBar()
{
  static const std::vector<Menu> menus = {
      {"&File",
       {item(MenuItem::NewConfiguration), cmd(Command::Open), item(MenuItem::OpenRecent),
        cmd(Command::Save), cmd(Command::SaveAs), cmd(Command::Reset), separator(),
        cmd(Command::Download), item(MenuItem::SaveErrorLog), separator(),
        item(MenuItem::Quit)}},
      {"&Tag",
       {cmd(Command::Attach), cmd(Command::Detach), separator(), cmd(Command::Read),
        cmd(Command::Start), cmd(Command::Stop), separator(), cmd(Command::Sync),
        cmd(Command::Test), separator(), cmd(Command::Erase)}},
      {"&Log", {item(MenuItem::LogLevel), item(MenuItem::ClearErrorLog)}},
      {"&Help", {item(MenuItem::UserGuide), item(MenuItem::About)}},
  };
  return menus;
}

const std::vector<MenuEntry> &tagPopup()
{
  static const std::vector<MenuEntry> entries = {
      cmd(Command::Attach), cmd(Command::Detach), cmd(Command::Sync),
      cmd(Command::Test),   cmd(Command::Erase),  cmd(Command::Download)};
  return entries;
}

const std::vector<MenuEntry> &configPopup()
{
  static const std::vector<MenuEntry> entries = {
      cmd(Command::Read),   cmd(Command::Open),  cmd(Command::Save), cmd(Command::SaveAs),
      cmd(Command::Reset),  separator(),         cmd(Command::Start)};
  return entries;
}

} // namespace qtmon
