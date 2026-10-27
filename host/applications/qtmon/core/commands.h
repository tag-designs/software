/**
 * @file    commands.h
 * @brief   qtmonitor's commands, and the one rule that decides when each is
 *          available.
 *
 * @details The window applies availability() to its actions; the menus, the
 *          popup menus and the status strip's Start and Stop buttons all
 *          follow it. actionStateTable() prints the rule for every
 *          combination of its inputs, and is compared with
 *          host/applications/qtmon/test/action-states.txt. Qt-free; see
 *          host/applications/qtmon/design/architecture.md.
 */

#ifndef QTMON_CORE_COMMANDS_H
#define QTMON_CORE_COMMANDS_H

#include <array>
#include <string>
#include <vector>

#include "tag.pb.h"

namespace qtmon {

/// Every command whose availability depends on the tag or the document.
enum class Command {
  Attach,    ///< Tag ▸ Attach.
  Detach,    ///< Tag ▸ Detach.
  Read,      ///< Tag ▸ Read Configuration.
  Start,     ///< Tag ▸ Start, and the Start button.
  Stop,      ///< Tag ▸ Stop, and the Stop button.
  Sync,      ///< Tag ▸ Synchronize Clock.
  Test,      ///< Tag ▸ Self-Test.
  Erase,     ///< Tag ▸ Erase….
  Download,  ///< File ▸ Download Data….
  Open,      ///< File ▸ Open Configuration….
  Save,      ///< File ▸ Save Configuration.
  SaveAs,    ///< File ▸ Save Configuration As….
  Reset,     ///< File ▸ Reset to Defaults.
};

/// Every Command, in the order of action-states.txt's columns.
inline constexpr std::array<Command, 13> kAllCommands = {
    Command::Attach, Command::Detach, Command::Read,     Command::Start, Command::Stop,
    Command::Sync,   Command::Test,   Command::Erase,    Command::Download,
    Command::Open,   Command::Save,   Command::SaveAs,   Command::Reset};

/// How a command is offered.
enum class Availability {
  Enabled,   ///< Shown and can be chosen.
  Disabled,  ///< Shown greyed out.
  Hidden,    ///< Not shown. No command is hidden at present; kept for the table.
};

/// Everything availability() reads.
struct CommandInputs {
  bool attached = false;                   ///< A tag (live or fixture) is attached.
  TagState state = STATE_UNSPECIFIED;      ///< The tag's state at the last status.
  TagType tag_type = TAG_UNSPECIFIED;      ///< The attached tag's type.
  bool document = false;                   ///< The Configuration tab holds a document.
  TagType document_type = TAG_UNSPECIFIED; ///< The document's tag type.
  bool downloading = false;                ///< A download is running.
  bool test_pending = false;               ///< Self-Test was sent; no result yet.
};

/**
 * @brief Whether @p command is available given @p in.
 *
 * @details Tag commands need an attached tag in the right state: Read,
 *          Synchronize Clock and Self-Test an IDLE one (Self-Test not while
 *          one is pending); Stop a CONFIGURED, RUNNING, HIBERNATING or
 *          CALIBRATE one; Erase a FINISHED or ABORTED one; Download a
 *          downloadable one (isDownloadableState()) while no download runs.
 *          Configuration commands need no tag: Save, Save As and Reset need a
 *          document, and Open is always available. Start needs an IDLE tag
 *          and a document of that tag's type.
 */
Availability availability(Command command, const CommandInputs &in);

/// The column name of @p command in action-states.txt ("attach", "saveas", …).
const char *commandName(Command command);

/**
 * @brief The input combinations actionStateTable() covers, in its order.
 *
 * @details For two tag types (BITTAG_LE and COMPASSTAG): detached with no
 *          document and with one; attached in every tag state with a document
 *          of the tag's type; then attached and IDLE with no document, with a
 *          document of another type (IMUTAG, or BITTAG_LE for an IMUTAG), with
 *          a self-test pending, and FINISHED with a download running. The
 *          window's wiring check (`qtmonitor --check-action-wiring`) drives
 *          the same cases through its actions.
 */
std::vector<CommandInputs> actionStateCases();

/**
 * @brief availability() for every case of actionStateCases(); the text of
 *        `qtmonitor --check-action-states`.
 * @return A header line, then one line per case, each ending "\n". A
 *         case's document is written "none", "same" (the tag's type) or
 *         "other"; each command is E, D or H.
 */
std::string actionStateTable();

/// "E", "D" or "H", as action-states.txt writes an Availability.
const char *availabilityLetter(Availability a);

} // namespace qtmon

#endif // QTMON_CORE_COMMANDS_H
