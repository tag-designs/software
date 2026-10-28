//! Tag Monitor's commands, and the one rule that decides when each is
//! available.
//!
//! Port of `host/applications/qtmon/core/commands.h`. [`action_state_table`]
//! prints the rule for every case of [`action_state_cases`], in exactly the
//! text `qtmonitor --check-action-states` prints, so the two are compared
//! with `host/applications/qtmon/test/action-states.txt`.

use tagcore::proto::{DESCRIPTOR_POOL, TagState, TagType};

use super::tagrules::is_downloadable_state;

/// Every command whose availability depends on the tag or the document.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum Command {
    /// Tag > Attach.
    Attach,
    /// Tag > Detach.
    Detach,
    /// Tag > Read Configuration.
    Read,
    /// Tag > Start, and the Start button.
    Start,
    /// Tag > Stop, and the Stop button.
    Stop,
    /// Tag > Synchronize Clock.
    Sync,
    /// Tag > Self-Test.
    Test,
    /// Tag > Erase...
    Erase,
    /// File > Download Data...
    Download,
    /// File > Open Configuration...
    Open,
    /// File > Save Configuration...: always asks for the file.
    Save,
    /// File > Reset to Defaults.
    Reset,
}

/// Every [`Command`], in the order of action-states.txt's columns.
pub const ALL_COMMANDS: [Command; 12] = [
    Command::Attach,
    Command::Detach,
    Command::Read,
    Command::Start,
    Command::Stop,
    Command::Sync,
    Command::Test,
    Command::Erase,
    Command::Download,
    Command::Open,
    Command::Save,
    Command::Reset,
];

/// How a command is offered.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Availability {
    /// Shown and can be chosen.
    Enabled,
    /// Shown greyed out.
    Disabled,
    /// Not shown. No command is hidden at present; kept for the table.
    Hidden,
}

/// Everything [`availability`] reads.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CommandInputs {
    /// A tag (live or fixture) is attached.
    pub attached: bool,
    /// The tag's state at the last status.
    pub state: TagState,
    /// The attached tag's type.
    pub tag_type: TagType,
    /// The Configuration tab holds a document.
    pub document: bool,
    /// The document's tag type.
    pub document_type: TagType,
    /// A download is running.
    pub downloading: bool,
    /// Self-Test was sent; no result yet.
    pub test_pending: bool,
}

impl Default for CommandInputs {
    fn default() -> Self {
        CommandInputs {
            attached: false,
            state: TagState::StateUnspecified,
            tag_type: TagType::TagUnspecified,
            document: false,
            document_type: TagType::TagUnspecified,
            downloading: false,
            test_pending: false,
        }
    }
}

/// Availability from a plain condition.
fn enabled_if(condition: bool) -> Availability {
    if condition {
        Availability::Enabled
    } else {
        Availability::Disabled
    }
}

/// Whether `command` is available given `input`.
///
/// Tag commands need an attached tag in the right state: Read, Synchronize
/// Clock and Self-Test an IDLE one (Self-Test not while one is pending);
/// Stop a CONFIGURED, RUNNING, HIBERNATING or CALIBRATE one; Erase a
/// FINISHED or ABORTED one; Download a downloadable one while no download
/// runs. Configuration commands need no tag: Save and Reset need a document,
/// and Open is always available. Start needs an IDLE tag and a document of
/// that tag's type.
pub fn availability(command: Command, input: &CommandInputs) -> Availability {
    let idle = input.attached && input.state == TagState::Idle;
    match command {
        Command::Attach => enabled_if(!input.attached),
        Command::Detach => enabled_if(input.attached),
        Command::Read | Command::Sync => enabled_if(idle),
        Command::Test => enabled_if(idle && !input.test_pending),
        Command::Start => {
            enabled_if(input.document && idle && input.document_type == input.tag_type)
        }
        // CALIBRATE is entered only from the command line (tag-cal), but Tag
        // Monitor can still stop a tag left in it.
        Command::Stop => enabled_if(
            input.attached
                && matches!(
                    input.state,
                    TagState::Configured
                        | TagState::Running
                        | TagState::Hibernating
                        | TagState::Calibrate
                ),
        ),
        Command::Erase => enabled_if(
            input.attached && matches!(input.state, TagState::Finished | TagState::Aborted),
        ),
        Command::Download => {
            enabled_if(input.attached && is_downloadable_state(input.state) && !input.downloading)
        }
        Command::Open => Availability::Enabled,
        Command::Save | Command::Reset => enabled_if(input.document),
    }
}

/// The column name of `command` in action-states.txt (`"attach"`, ...).
pub fn command_name(command: Command) -> &'static str {
    match command {
        Command::Attach => "attach",
        Command::Detach => "detach",
        Command::Read => "read",
        Command::Start => "start",
        Command::Stop => "stop",
        Command::Sync => "sync",
        Command::Test => "test",
        Command::Erase => "erase",
        Command::Download => "download",
        Command::Open => "open",
        Command::Save => "save",
        Command::Reset => "reset",
    }
}

/// `"E"`, `"D"` or `"H"`, as action-states.txt writes an [`Availability`].
pub fn availability_letter(a: Availability) -> &'static str {
    match a {
        Availability::Enabled => "E",
        Availability::Disabled => "D",
        Availability::Hidden => "H",
    }
}

/// Every tag state, in the order `proto/tag.proto` declares them, as the
/// C++ iterates `TagState_descriptor()`.
pub fn tag_states_in_order() -> Vec<TagState> {
    let descriptor = DESCRIPTOR_POOL
        .get_enum_by_name("TagState")
        .expect("TagState is in the descriptor pool");
    descriptor
        .values()
        .filter_map(|v| TagState::try_from(v.number()).ok())
        .collect()
}

/// The input combinations [`action_state_table`] covers, in its order.
///
/// For two tag types (BITTAG_LE and COMPASSTAG): detached with no document
/// and with one; attached in every tag state with a document of the tag's
/// type; then attached and IDLE with no document, with a document of another
/// type (IMUTAG, or BITTAG_LE for an IMUTAG), with a self-test pending, and
/// FINISHED with a download running.
pub fn action_state_cases() -> Vec<CommandInputs> {
    let mut cases = Vec::new();
    let mut add = |attached: bool,
                   state: TagState,
                   tag_type: TagType,
                   document: &str,
                   downloading: bool,
                   pending: bool| {
        let document_type = match document {
            "none" => TagType::TagUnspecified,
            "same" => tag_type,
            _ if tag_type == TagType::Imutag => TagType::BittagLe,
            _ => TagType::Imutag,
        };
        cases.push(CommandInputs {
            attached,
            state,
            tag_type,
            document: document != "none",
            document_type,
            downloading,
            test_pending: pending,
        });
    };
    for tag_type in [TagType::BittagLe, TagType::Compasstag] {
        for document in ["none", "same"] {
            add(false, TagState::Idle, tag_type, document, false, false);
        }
        for state in tag_states_in_order() {
            add(true, state, tag_type, "same", false, false);
        }
        add(true, TagState::Idle, tag_type, "none", false, false);
        add(true, TagState::Idle, tag_type, "other", false, false);
        add(true, TagState::Idle, tag_type, "same", false, true);
        add(true, TagState::Finished, tag_type, "same", true, false);
    }
    cases
}

/// [`availability`] for every case of [`action_state_cases`]; the text of
/// `qtmonitor --check-action-states`.
///
/// A header line, then one line per case, each ending `"\n"`. A case's
/// document is written `none`, `same` (the tag's type) or `other`; each
/// command is E, D or H.
pub fn action_state_table() -> String {
    let mut out = String::from("attached state tagtype document downloading testpending |");
    for c in ALL_COMMANDS {
        out += " ";
        out += command_name(c);
    }
    out += "\n";
    for input in action_state_cases() {
        let document = if !input.document {
            "none"
        } else if input.document_type == input.tag_type {
            "same"
        } else {
            "other"
        };
        out += &format!(
            "{} {} {} {} {} {} |",
            u8::from(input.attached),
            input.state.as_str_name(),
            input.tag_type.as_str_name(),
            document,
            u8::from(input.downloading),
            u8::from(input.test_pending)
        );
        for c in ALL_COMMANDS {
            out += &format!(
                " {}={}",
                command_name(c),
                availability_letter(availability(c, &input))
            );
        }
        out += "\n";
    }
    out
}
