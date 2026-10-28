//! The Configuration tab without its widgets: the document and the form
//! together.
//!
//! Port of `host/applications/qtmon/tagconfiguration/configtab.cpp`'s logic:
//! loading a document into the form (with its unsaved-changes baseline),
//! New, Open and Reset, what Start sends, and when the form can be edited.
//! The dialogs (which file, discard changes?) are the engine's; this holds
//! only the rules, so the probe test (`tests/config_probe.rs`) can run every
//! case through it.

use serde_json::Value;
use tagcore::configshape::ConfigShape;
use tagcore::proto::{Config, TagState, TagType};

use super::configdocument::{ConfigDocument, DocumentSource, ParseFileError};
use super::configform::ConfigForm;

/// How opening a configuration's text went.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum OpenResult {
    /// It is the document now.
    Opened,
    /// The text is not a valid Config; the string says why.
    ParseError(String),
    /// Its tag type has no default config; the string says which.
    NoDefaults(String),
}

/// The configuration document and the form that edits it.
#[derive(Debug, Clone, Default)]
pub struct ConfigEditor {
    /// The document: values, source, file, baseline and approval.
    doc: ConfigDocument,
    /// The panels.
    form: ConfigForm,
    /// A tag is attached (a state has been reported since the last detach).
    attached: bool,
    /// The attached tag's last reported state.
    state: Option<TagState>,
}

impl ConfigEditor {
    /// No document; the form built and empty.
    pub fn new() -> Self {
        Self::default()
    }

    /// The document.
    pub fn document(&self) -> &ConfigDocument {
        &self.doc
    }
    /// The form.
    pub fn form(&self) -> &ConfigForm {
        &self.form
    }

    /// The document's name, as the title shows it: `"BITTAG defaults"`, a
    /// file name, `"BITTAG from tag"`, or empty.
    pub fn document_name(&self) -> String {
        let tag_type = self.doc.tag_type().as_str_name();
        match self.doc.source() {
            DocumentSource::None => String::new(),
            DocumentSource::Defaults => format!("{tag_type} defaults"),
            DocumentSource::File => std::path::Path::new(self.doc.path())
                .file_name()
                .map(|n| n.to_string_lossy().into_owned())
                .unwrap_or_default(),
            DocumentSource::Tag => format!("{tag_type} from tag"),
        }
    }

    /// What Start would send, with no document `None`. `now` is the clock,
    /// seconds.
    pub fn collect(&self, now: i64) -> Option<Config> {
        self.doc.has_document().then(|| {
            self.form
                .collect(self.doc.config(), self.doc.tag_type(), now)
        })
    }

    /// What Start sends (ConfigTab::GetConfig): [`collect`](Self::collect),
    /// recorded as the document's values.
    pub fn get_config(&mut self, now: i64) -> Option<Config> {
        let config = self.collect(now)?;
        self.doc.set_config(config.clone());
        Some(config)
    }

    /// Whether the form's values differ from what it sent after loading.
    pub fn is_modified(&self, now: i64) -> bool {
        self.doc.is_modified(self.collect(now).as_ref())
    }

    /// Makes `config` the document and loads the form (ConfigTab::load).
    /// A reset keeps the old baseline, so it shows as unsaved changes.
    /// Withdraws approval. Returns any message a panel raised.
    pub fn load(
        &mut self,
        config: Config,
        source: DocumentSource,
        path: &str,
        keep_baseline: bool,
        now: i64,
    ) -> Vec<String> {
        self.doc.load(config.clone(), source, path);
        let dialogs = self.form.load(&config, self.doc.tag_type(), now);
        if !keep_baseline {
            let written = self.collect(now);
            self.doc.set_baseline(written.as_ref());
        }
        self.update_editable();
        self.doc.content_changed();
        dialogs
    }

    /// The attached tag's configuration as the document (SetConfig).
    pub fn set_config(&mut self, config: Config, now: i64) -> Vec<String> {
        self.load(config, DocumentSource::Tag, "", false, now)
    }

    /// New Configuration: the tag type's defaults; `None` when it has none.
    pub fn load_defaults(&mut self, tag_type: TagType, now: i64) -> Option<Vec<String>> {
        let defaults = ConfigShape::default_config(tag_type)?;
        Some(self.load(defaults, DocumentSource::Defaults, "", false, now))
    }

    /// Reset to Defaults: the values become the tag type's defaults, the
    /// document (source, file, baseline) stays. Nothing without a document.
    pub fn reset_to_defaults(&mut self, now: i64) -> Vec<String> {
        match ConfigShape::default_config(self.doc.tag_type()) {
            Some(defaults) if self.doc.has_document() => {
                let (source, path) = (self.doc.source(), self.doc.path().to_string());
                self.load(defaults, source, &path, true, now)
            }
            _ => Vec::new(),
        }
    }

    /// Opens a configuration's text (OpenFromJsonText): onto its tag type's
    /// defaults, `fallback` when it names none; `path` is kept for Save.
    /// A failure leaves the document as it was.
    pub fn open_text(
        &mut self,
        text: &str,
        fallback: TagType,
        path: &str,
        now: i64,
    ) -> (OpenResult, Vec<String>) {
        match ConfigDocument::parse_file(text, fallback) {
            Ok(config) => (
                OpenResult::Opened,
                self.load(config, DocumentSource::File, path, false, now),
            ),
            Err(ParseFileError::Parse(d)) => (OpenResult::ParseError(d), Vec::new()),
            Err(ParseFileError::NoDefaults(d)) => (OpenResult::NoDefaults(d), Vec::new()),
        }
    }

    /// Saved to `path` with `written` (what Start would send) as its values.
    pub fn saved(&mut self, path: &str, written: Config) {
        self.doc.saved(path, written);
    }

    /// Unloads the document.
    pub fn clear(&mut self) {
        self.form.clear();
        self.doc.clear();
        self.update_editable();
    }

    /// The attached tag reported `state` (ConfigTab::StateUpdate).
    pub fn state_update(&mut self, state: TagState) {
        self.attached = true;
        self.state = Some(state);
        self.update_editable();
    }

    /// The tag was detached.
    pub fn detach(&mut self) {
        self.attached = false;
        self.update_editable();
    }

    /// The tag's own configuration cannot be edited while that tag is
    /// attached and not IDLE (ConfigTab::updateEditable).
    fn update_editable(&mut self) {
        let running_tags_own = self.doc.source() == DocumentSource::Tag
            && self.attached
            && self.state != Some(TagState::Idle);
        self.form.set_enabled(!running_tags_own);
    }

    /// Whether the user approved the values for Start.
    pub fn approved(&self) -> bool {
        self.doc.approved()
    }
    /// Records the user's approval (ignored with no document).
    pub fn set_approved(&mut self, approved: bool) {
        self.doc.set_approved(approved);
    }

    /// One user edit of a control (see [`ConfigForm::edit`]); an edit
    /// withdraws approval. False when nothing took it.
    pub fn edit(&mut self, id: &str, value: &Value) -> bool {
        let taken = self.form.edit(id, value);
        if taken {
            self.doc.content_changed();
        }
        taken
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    /// The probe cases' clock: 2026-10-08 12:22:03 UTC.
    const NOW: i64 = 1_791_460_923;

    /// The value of control `id` in the form's view.
    fn value(editor: &ConfigEditor, id: &str) -> Value {
        editor
            .form()
            .controls()
            .into_iter()
            .find(|c| c.id == id)
            .map(|c| c.view()["value"].clone())
            .unwrap_or(Value::Null)
    }

    /// An edit marks the document modified and withdraws approval; the
    /// value sent follows the edit.
    #[test]
    fn an_edit_modifies_and_withdraws_approval() {
        let mut e = ConfigEditor::new();
        e.load_defaults(TagType::Prestag, NOW).unwrap();
        e.set_approved(true);
        assert!(!e.is_modified(NOW));
        assert!(e.edit("period", &json!(120)));
        assert!(e.is_modified(NOW) && !e.approved());
        assert_eq!(e.get_config(NOW).unwrap().period, 120);
        // The spin box clamps, as QSpinBox does.
        e.edit("period", &json!(1000));
        assert_eq!(value(&e, "period"), json!(300));
    }

    /// Choosing a range re-limits both thresholds and clamps them, as the
    /// Standard ADXL362 panel's range buttons do.
    #[test]
    fn the_range_clamps_the_thresholds() {
        let mut e = ConfigEditor::new();
        e.load_defaults(TagType::Bittag, NOW).unwrap();
        e.edit("adxl.act", &json!(1.9));
        e.edit("adxl.range", &json!(1)); // ±2 g: thresholds up to 1 g
        assert_eq!(value(&e, "adxl.act"), json!(1.0));
        assert_eq!(
            e.get_config(NOW).unwrap().adxl362.unwrap().act_thresh_g,
            1.0
        );
    }

    /// A hibernation row first put in use starts at the logging start,
    /// rounded up to the hour, and lasts an hour.
    #[test]
    fn a_new_hibernation_row_starts_at_the_logging_start() {
        let mut e = ConfigEditor::new();
        e.load_defaults(TagType::Bittag, NOW).unwrap();
        assert!(e.edit("hib0.in_use", &json!(true)));
        let start = value(&e, "start").as_i64().unwrap();
        assert_eq!(value(&e, "hib0.start"), json!(start));
        assert_eq!(value(&e, "hib0.end"), json!(start + 3600));
        let sent = e.get_config(NOW).unwrap();
        assert_eq!(sent.hibernate[0].start_epoch as i64, start);
    }

    /// The tag's own configuration is read-only while that tag runs.
    #[test]
    fn a_running_tags_own_configuration_is_read_only() {
        let mut e = ConfigEditor::new();
        e.set_config(ConfigShape::default_config(TagType::Prestag).unwrap(), NOW);
        e.state_update(TagState::Running);
        assert!(!e.edit("period", &json!(30)));
        e.state_update(TagState::Idle);
        assert!(e.edit("period", &json!(30)));
    }

    /// Start On a time in the past is sent as immediately (0).
    #[test]
    fn a_start_in_the_past_is_sent_as_immediately() {
        let mut e = ConfigEditor::new();
        e.load_defaults(TagType::Compasstag, NOW).unwrap();
        e.edit("start_mode", &json!(1));
        let start = value(&e, "start").as_i64().unwrap();
        assert_eq!(
            e.get_config(NOW)
                .unwrap()
                .active_interval
                .unwrap()
                .start_epoch as i64,
            start
        );
        assert_eq!(
            e.get_config(start + 1)
                .unwrap()
                .active_interval
                .unwrap()
                .start_epoch,
            0
        );
    }
}
