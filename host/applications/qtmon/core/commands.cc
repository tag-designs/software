/**
 * @file    commands.cc
 * @brief   The availability rule and its table, declared in commands.h.
 */

#include "commands.h"

#include "tagrules.h"

namespace qtmon {

namespace {

/// Availability from a plain condition.
Availability enabledIf(bool condition)
{
  return condition ? Availability::Enabled : Availability::Disabled;
}

} // namespace

Availability availability(Command command, const CommandInputs &in)
{
  const bool idle = in.attached && in.state == IDLE;
  switch (command) {
  case Command::Attach:
    return enabledIf(!in.attached);
  case Command::Detach:
    return enabledIf(in.attached);
  case Command::Read:
  case Command::Sync:
    return enabledIf(idle);
  case Command::Test:
    return enabledIf(idle && !in.test_pending);
  case Command::Start:
    return enabledIf(in.document && idle && in.document_type == in.tag_type);
  case Command::Stop:
    // CALIBRATE is entered only from the command line (tag-cal), but
    // qtmonitor can still stop a tag left in it.
    return enabledIf(in.attached && (in.state == CONFIGURED || in.state == RUNNING ||
                                     in.state == HIBERNATING || in.state == CALIBRATE));
  case Command::Erase:
    return enabledIf(in.attached && (in.state == FINISHED || in.state == ABORTED));
  case Command::Download:
    return enabledIf(in.attached && isDownloadableState(in.state) && !in.downloading);
  case Command::Open:
    return Availability::Enabled;
  case Command::Save:
  case Command::SaveAs:
  case Command::Reset:
    return enabledIf(in.document);
  }
  return Availability::Disabled;
}

const char *commandName(Command command)
{
  switch (command) {
  case Command::Attach:
    return "attach";
  case Command::Detach:
    return "detach";
  case Command::Read:
    return "read";
  case Command::Start:
    return "start";
  case Command::Stop:
    return "stop";
  case Command::Sync:
    return "sync";
  case Command::Test:
    return "test";
  case Command::Erase:
    return "erase";
  case Command::Download:
    return "download";
  case Command::Open:
    return "open";
  case Command::Save:
    return "save";
  case Command::SaveAs:
    return "saveas";
  case Command::Reset:
    return "reset";
  }
  return "?";
}

const char *availabilityLetter(Availability a)
{
  switch (a) {
  case Availability::Enabled:
    return "E";
  case Availability::Disabled:
    return "D";
  case Availability::Hidden:
    return "H";
  }
  return "?";
}

std::vector<CommandInputs> actionStateCases()
{
  std::vector<CommandInputs> cases;
  // document: "none", "same" (the tag's type) or "other" (another type).
  auto add = [&](bool attached, TagState state, TagType type, const std::string &document,
                 bool downloading, bool pending) {
    CommandInputs in;
    in.attached = attached;
    in.state = state;
    in.tag_type = type;
    in.document = document != "none";
    in.document_type = document == "none"   ? TAG_UNSPECIFIED
                       : document == "same" ? type
                                            : (type == IMUTAG ? BITTAG_LE : IMUTAG);
    in.downloading = downloading;
    in.test_pending = pending;
    cases.push_back(in);
  };
  const auto *states = TagState_descriptor();
  for (TagType type : {BITTAG_LE, COMPASSTAG}) {
    for (const char *document : {"none", "same"})
      add(false, IDLE, type, document, false, false);
    for (int i = 0; i < states->value_count(); ++i)
      add(true, static_cast<TagState>(states->value(i)->number()), type, "same", false, false);
    add(true, IDLE, type, "none", false, false);    // attached, no document
    add(true, IDLE, type, "other", false, false);   // document for another type
    add(true, IDLE, type, "same", false, true);     // self-test pending
    add(true, FINISHED, type, "same", true, false); // download running
  }
  return cases;
}

std::string actionStateTable()
{
  std::string out = "attached state tagtype document downloading testpending |";
  for (Command c : kAllCommands)
    out += std::string(" ") + commandName(c);
  out += "\n";
  for (const CommandInputs &in : actionStateCases()) {
    const char *document = !in.document                       ? "none"
                           : in.document_type == in.tag_type ? "same"
                                                              : "other";
    out += std::to_string(in.attached) + " " + TagState_Name(in.state) + " " +
           TagType_Name(in.tag_type) + " " + document + " " + std::to_string(in.downloading) +
           " " + std::to_string(in.test_pending) + " |";
    for (Command c : kAllCommands)
      out += std::string(" ") + commandName(c) + "=" + availabilityLetter(availability(c, in));
    out += "\n";
  }
  return out;
}

} // namespace qtmon
