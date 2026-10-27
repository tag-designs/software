//! Tag Monitor's configuration document: one Config, where it came from,
//! whether it has unsaved changes, and whether the user has approved it for
//! Start.
//!
//! Port of `host/applications/qtmon/core/configdocument.h`. Reading a
//! configuration file onto its tag type's defaults, the text a save writes,
//! the unsaved-changes baseline and approval are all here, with no UI.

use prost::Message;
use prost_reflect::{ReflectMessage, SerializeOptions};
use tagcore::configshape::ConfigShape;
use tagcore::proto::{self, Config, TagType};

/// Where the document came from.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum DocumentSource {
    /// No document.
    #[default]
    None,
    /// A tag type's default configuration (New).
    Defaults,
    /// A configuration file (Open, or after Save).
    File,
    /// The attached tag's configuration (attach, Read).
    Tag,
}

/// What attaching a tag does to the document ([`on_attach`]).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum OnAttach {
    /// Keep the document: the user opened or created it, and may be about to
    /// start this tag with it.
    Keep,
    /// Make the tag's configuration the document.
    Replace,
    /// Replace a previous tag's configuration, asking first if it has unsaved
    /// changes.
    ReplaceIfDiscarded,
}

/// What attaching a tag does to a document from `source`: keep defaults and
/// files, replace a previous tag's after asking, replace no document.
pub fn on_attach(source: DocumentSource) -> OnAttach {
    match source {
        DocumentSource::Defaults | DocumentSource::File => OnAttach::Keep,
        DocumentSource::Tag => OnAttach::ReplaceIfDiscarded,
        DocumentSource::None => OnAttach::Replace,
    }
}

/// Why a configuration file could not be read ([`ConfigDocument::parse_file`]).
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ParseFileError {
    /// The text is not a valid Config; the string says why.
    Parse(String),
    /// Its tag type has no default config; the string says which.
    NoDefaults(String),
}

/// The configuration being edited: its values, source, file,
/// unsaved-changes baseline and approval.
///
/// "Modified" compares what the form would send now with what it sent just
/// after loading (the baseline), because only the form knows how its fields
/// round and clamp; the caller passes both in. Approval is of the values to
/// be sent, so every load, reset, clear or edit withdraws it
/// ([`content_changed`](Self::content_changed)); saving does not.
#[derive(Debug, Clone, Default)]
pub struct ConfigDocument {
    /// The values as last loaded, sent or saved.
    config: Config,
    /// The document's tag type.
    tag_type: TagType,
    /// Where it came from.
    source: DocumentSource,
    /// Its file, for [`DocumentSource::File`].
    path: String,
    /// What the form sent after loading, serialised; `None` when it could not.
    baseline: Option<Vec<u8>>,
    /// The user's approval for Start.
    approved: bool,
}

impl ConfigDocument {
    /// True when a document is loaded.
    pub fn has_document(&self) -> bool {
        self.source != DocumentSource::None
    }
    /// Where the document came from.
    pub fn source(&self) -> DocumentSource {
        self.source
    }
    /// The document's tag type; TAG_UNSPECIFIED with no document.
    pub fn tag_type(&self) -> TagType {
        if self.has_document() {
            self.tag_type
        } else {
            TagType::TagUnspecified
        }
    }
    /// The document's file, for source File; empty otherwise.
    pub fn path(&self) -> &str {
        &self.path
    }
    /// The values as last loaded, sent or saved.
    pub fn config(&self) -> &Config {
        &self.config
    }

    /// Makes `config` the document and withdraws approval. The caller then
    /// sets the baseline, except for a reset, which keeps the old one so the
    /// reset shows as unsaved changes.
    pub fn load(&mut self, config: Config, source: DocumentSource, path: &str) {
        self.tag_type = config.tag_type();
        self.config = config;
        self.source = source;
        self.path = path.to_string();
        self.approved = false;
    }

    /// Records what the form sent just after loading, or `None` when it
    /// could not send the loaded values (every later state then counts as
    /// modified).
    pub fn set_baseline(&mut self, written: Option<&Config>) {
        self.baseline = written.map(Message::encode_to_vec);
    }

    /// Whether the form's values differ from the baseline: false with no
    /// document; true when `written` is `None` or differs.
    pub fn is_modified(&self, written: Option<&Config>) -> bool {
        if !self.has_document() {
            return false;
        }
        match (written, &self.baseline) {
            (Some(now), Some(base)) => &now.encode_to_vec() != base,
            _ => true,
        }
    }

    /// Records values that were sent or saved as the document's values.
    pub fn set_config(&mut self, config: Config) {
        self.config = config;
    }

    /// The document was saved to `path` with `written` as its values: it
    /// becomes a file document with no unsaved changes. Approval is
    /// unchanged, since the values are.
    pub fn saved(&mut self, path: &str, written: Config) {
        self.baseline = Some(written.encode_to_vec());
        self.config = written;
        self.source = DocumentSource::File;
        self.path = path.to_string();
    }

    /// Unloads the document and withdraws approval.
    pub fn clear(&mut self) {
        *self = ConfigDocument::default();
    }

    /// Whether the user has approved the values for Start.
    pub fn approved(&self) -> bool {
        self.approved
    }
    /// Records the user's approval; ignored with no document.
    pub fn set_approved(&mut self, approved: bool) {
        self.approved = approved && self.has_document();
    }
    /// The values to be sent may have changed: withdraws approval.
    pub fn content_changed(&mut self) {
        self.approved = false;
    }

    /// Reads a configuration file as a document's values, without loading
    /// it: the tag type's defaults with the file merged on top, so a full
    /// file loads as written and a partial one still gives a complete
    /// configuration. The tag type is the file's `tag_type`, or
    /// `fallback_type` when it names none.
    ///
    /// # Errors
    ///
    /// [`ParseFileError::Parse`] when the text is not a valid Config;
    /// [`ParseFileError::NoDefaults`] when its tag type has no defaults.
    pub fn parse_file(json_text: &str, fallback_type: TagType) -> Result<Config, ParseFileError> {
        let parsed: Config = proto::from_json(json_text).map_err(ParseFileError::Parse)?;
        let file_shape = ConfigShape::for_file(json_text).map_err(ParseFileError::Parse)?;
        let tag_type = if parsed.tag_type() != TagType::TagUnspecified {
            parsed.tag_type()
        } else {
            fallback_type
        };
        let mut document = ConfigShape::default_config(tag_type).ok_or_else(|| {
            ParseFileError::NoDefaults(format!(
                "No default configuration for tag type {}",
                tag_type.as_str_name()
            ))
        })?;
        ConfigShape::for_tag(tag_type).merge(&mut document, &parsed, &file_shape);
        Ok(document)
    }

    /// Whether `json_text` is a valid Config that names no tag type, so the
    /// user must be asked which it is for.
    pub fn names_no_tag_type(json_text: &str) -> bool {
        proto::from_json::<Config>(json_text).is_ok_and(|c| c.tag_type() == TagType::TagUnspecified)
    }

    /// The text Save writes: indented protobuf JSON with every field,
    /// including those at their default, under its proto field name.
    pub fn file_text(config: &Config) -> String {
        let dynamic = config.transcode_to_dynamic();
        let mut out = serde_json::Serializer::pretty(Vec::new());
        dynamic
            .serialize_with_options(
                &mut out,
                &SerializeOptions::new()
                    .use_proto_field_name(true)
                    .skip_default_fields(false),
            )
            .expect("a Config always serializes to JSON");
        String::from_utf8(out.into_inner()).expect("JSON is UTF-8")
    }
}
