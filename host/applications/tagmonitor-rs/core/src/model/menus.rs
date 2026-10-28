//! Tag Monitor's menus as data: what each command is called, its tooltip
//! and shortcut, and where it appears.
//!
//! Port of `host/applications/qtmon/core/menus.h`. The UI builds its menu bar
//! from [`menu_bar`], and every popup menu shows the same menus as submenus.
//! Which entries are enabled is a separate rule,
//! [`availability`](super::commands::availability).

use super::commands::Command;

/// A standard shortcut, which each platform maps to its own keys.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Shortcut {
    /// No shortcut.
    None,
    /// Ctrl+O / Cmd+O.
    Open,
    /// Ctrl+S / Cmd+S.
    Save,
    /// The platform's quit key.
    Quit,
}

/// How an entry is labelled.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct EntryText {
    /// The menu text; "..." marks an entry that asks for more.
    pub label: &'static str,
    /// Hover text; empty for none.
    pub tooltip: &'static str,
    /// Its shortcut, if any.
    pub shortcut: Shortcut,
}

/// An [`EntryText`] in one line.
const fn text(label: &'static str, tooltip: &'static str, shortcut: Shortcut) -> EntryText {
    EntryText {
        label,
        tooltip,
        shortcut,
    }
}

/// The text, tooltip and shortcut of `command`.
pub fn command_text(command: Command) -> EntryText {
    use Shortcut::None as N;
    match command {
        Command::Attach => text("Attach", "Find a tag base and attach to its tag", N),
        Command::Detach => text("Detach", "Detach from the tag", N),
        Command::Read => text("Read Configuration", "Read the tag's configuration", N),
        Command::Start => text(
            "Start",
            "Write the configuration to the tag and start it",
            N,
        ),
        Command::Stop => text("Stop", "Stop the tag", N),
        Command::Sync => text(
            "Synchronize Clock",
            "Set the tag's clock from this computer's",
            N,
        ),
        Command::Test => text("Self-Test", "Run the tag's self-tests", N),
        Command::Erase => text("Erase...", "Erase the tag's data and return it to IDLE", N),
        Command::Download => text(
            "Download Data...",
            "Download the tag's data log to a file",
            N,
        ),
        Command::Open => text(
            "Open Configuration...",
            "Open a configuration file for editing",
            Shortcut::Open,
        ),
        Command::Save => text(
            "Save Configuration...",
            "Save the configuration to a file",
            Shortcut::Save,
        ),
        Command::Reset => text(
            "Reset to Defaults",
            "Replace the configuration's values with its tag type's defaults",
            N,
        ),
    }
}

/// Menu entries that are not tag or document commands: they are always
/// available, so [`availability`](super::commands::availability) does not
/// cover them.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MenuItem {
    /// Submenu: one entry per tag type with a default configuration.
    NewConfiguration,
    /// Submenu: the recent configuration files.
    OpenRecent,
    /// Save Error Log...
    SaveErrorLog,
    /// Clear Error Log.
    ClearErrorLog,
    /// Submenu: the Error Log's level, TRACE to ERROR.
    LogLevel,
    /// Closes the window, which may ask to save.
    Quit,
    /// Opens Tag Monitor's page of the user manual.
    UserGuide,
    /// Version and purpose.
    About,
}

/// The text, tooltip and shortcut of `item`.
pub fn item_text(item: MenuItem) -> EntryText {
    use Shortcut::None as N;
    match item {
        MenuItem::NewConfiguration => text("New Configuration", "", N),
        MenuItem::OpenRecent => text("Open Recent", "", N),
        MenuItem::SaveErrorLog => text("Save Error Log...", "Save the error log to a text file", N),
        MenuItem::ClearErrorLog => text("Clear Error Log", "Clear the error log", N),
        MenuItem::LogLevel => text("Log Level", "", N),
        MenuItem::Quit => text("Quit", "", Shortcut::Quit),
        MenuItem::UserGuide => text(
            "Tag Monitor User Guide",
            "Open the Tag Monitor page of the user manual",
            N,
        ),
        MenuItem::About => text("About Tag Monitor", "Version and purpose", N),
    }
}

/// One entry in a menu.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MenuEntry {
    /// A command; see [`command_text`] and the availability rule.
    Command(Command),
    /// An always-available item; see [`item_text`].
    Item(MenuItem),
    /// A dividing line.
    Separator,
}

/// A menu in the menu bar.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Menu {
    /// With its mnemonic, e.g. `"&File"`.
    pub title: &'static str,
    /// In order.
    pub entries: Vec<MenuEntry>,
}

/// The menu bar: File, Tag, Log and Help, in order. The popup menus show
/// these same menus as submenus.
pub fn menu_bar() -> Vec<Menu> {
    use Command as C;
    use MenuEntry::{Command as Cmd, Item, Separator};
    vec![
        Menu {
            title: "&File",
            entries: vec![
                Item(MenuItem::NewConfiguration),
                Cmd(C::Open),
                Item(MenuItem::OpenRecent),
                Cmd(C::Save),
                Cmd(C::Reset),
                Separator,
                Cmd(C::Download),
                Item(MenuItem::SaveErrorLog),
                Separator,
                Item(MenuItem::Quit),
            ],
        },
        Menu {
            title: "&Tag",
            entries: vec![
                Cmd(C::Attach),
                Cmd(C::Detach),
                Separator,
                Cmd(C::Read),
                Cmd(C::Start),
                Cmd(C::Stop),
                Separator,
                Cmd(C::Sync),
                Cmd(C::Test),
                Separator,
                Cmd(C::Erase),
            ],
        },
        Menu {
            title: "&Log",
            entries: vec![Item(MenuItem::LogLevel), Item(MenuItem::ClearErrorLog)],
        },
        Menu {
            title: "&Help",
            entries: vec![Item(MenuItem::UserGuide), Item(MenuItem::About)],
        },
    ]
}
