//! Which [`Config`] fields a tag type supports, as dotted field paths.
//!
//! Port of `configshape.*`. proto3 does not record whether a zero-valued
//! scalar was set, so a decoded Config cannot say which fields a tag
//! supports. Each tag's checked-in default configuration does say:
//! `embedded/proto-c/<name>-proto-c/default-config.json` lists every field
//! the tag supports, zeros included. `build.rs` embeds those files, and
//! [`ConfigShape`] reads them into one set of paths per tag type, such as
//! `"adxl362.freq"` or `"period"`.
//!
//! A shape answers two questions: [`has`](ConfigShape::has) (does this tag
//! support this field?) and [`merge`](ConfigShape::merge) (copy from a
//! configuration file only the fields the tag supports and the file sets).
//! See `docs/shared/design/configuration-model.md`.

use std::collections::{BTreeMap, BTreeSet};
use std::sync::LazyLock;

use prost_reflect::{DynamicMessage, MessageDescriptor, ReflectMessage, Value};
use serde_json::Map;

use crate::proto::{Config, TagType, from_json};

/// Every tag's default-config.json: (directory name, text), by name.
const EMBEDDED_DEFAULT_CONFIGS: &[(&str, &str)] =
    include!(concat!(env!("OUT_DIR"), "/defaultconfigs.rs"));

/// A set of Config field paths, for one tag type or one JSON file.
///
/// A path names a field by its .proto name, with nested messages joined by
/// dots: `active_interval`, `active_interval.start_epoch`, `adxl362`,
/// `adxl362.range`. A message field is listed as well as the fields inside
/// it. A repeated message field (`hibernate`) is listed once, with the
/// fields of its elements under it (`hibernate.start_epoch`). `tag_type` is
/// never a path: it identifies the tag rather than configuring it.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct ConfigShape {
    paths: BTreeSet<String>,
}

/// Everything read from the embedded default configs.
struct Table {
    shapes: BTreeMap<i32, ConfigShape>,
    defaults: BTreeMap<i32, Config>,
    errors: Vec<String>,
}

static TABLE: LazyLock<Table> = LazyLock::new(build_table);

static VALID_PATHS: LazyLock<BTreeSet<String>> = LazyLock::new(|| {
    let mut out = BTreeSet::new();
    all_paths(&config_descriptor(), "", &mut out);
    out
});

fn config_descriptor() -> MessageDescriptor {
    Config::default().descriptor()
}

/// Every dotted path of every field reachable from `ty`, except tag_type.
fn all_paths(ty: &MessageDescriptor, prefix: &str, out: &mut BTreeSet<String>) {
    for f in ty.fields() {
        if prefix.is_empty() && f.name() == "tag_type" {
            continue;
        }
        let path = format!("{prefix}{}", f.name());
        out.insert(path.clone());
        if let Some(m) = f.kind().as_message() {
            all_paths(m, &format!("{path}."), out);
        }
    }
}

/// Records the path of every key under `object`, checked against `ty`.
/// Descends into objects and into the first element of arrays of objects.
/// Keys may be .proto or JSON names; a top-level tag_type is skipped.
fn walk(
    object: &Map<String, serde_json::Value>,
    ty: &MessageDescriptor,
    prefix: &str,
    paths: &mut BTreeSet<String>,
) -> Result<(), String> {
    for (key, value) in object {
        if prefix.is_empty() && key == "tag_type" {
            continue;
        }
        let field = ty
            .get_field_by_name(key)
            .or_else(|| ty.get_field_by_json_name(key))
            .ok_or_else(|| format!("no Config field {prefix}{key}"))?;
        let path = format!("{prefix}{}", field.name());
        paths.insert(path.clone());
        let Some(message) = field.kind().as_message().cloned() else {
            continue;
        };
        let child = match value {
            serde_json::Value::Object(o) => Some(o),
            serde_json::Value::Array(a) => a.first().and_then(|v| v.as_object()),
            _ => None,
        };
        if let Some(child) = child {
            walk(child, &message, &format!("{path}."), paths)?;
        }
    }
    Ok(())
}

/// The field paths present in `json`, and its top-level `tag_type` string
/// (empty when absent or not a string).
fn shape_of(json: &str) -> Result<(BTreeSet<String>, String), String> {
    let value: serde_json::Value =
        serde_json::from_str(json).map_err(|e| format!("not a JSON object: {e}"))?;
    let object = value
        .as_object()
        .ok_or_else(|| "not a JSON object: top level is not an object".to_owned())?;
    let tag_type = object
        .get("tag_type")
        .and_then(|v| v.as_str())
        .unwrap_or_default()
        .to_owned();
    let mut paths = BTreeSet::new();
    walk(object, &config_descriptor(), "", &mut paths)?;
    Ok((paths, tag_type))
}

/// A known, specified tag type named `name`.
fn parse_tag_type(name: &str) -> Option<TagType> {
    TagType::from_str_name(name).filter(|t| *t != TagType::TagUnspecified)
}

fn build_table() -> Table {
    let mut table = Table {
        shapes: BTreeMap::new(),
        defaults: BTreeMap::new(),
        errors: Vec::new(),
    };
    for (name, text) in EMBEDDED_DEFAULT_CONFIGS {
        let loaded = shape_of(text).and_then(|(paths, tag_name)| {
            let tag_type = parse_tag_type(&tag_name).ok_or("no valid tag_type")?;
            let config: Config =
                from_json(text).map_err(|_| "does not parse as a Config".to_owned())?;
            Ok((tag_type, paths, config))
        });
        match loaded {
            Ok((tag_type, paths, config)) => {
                let key = tag_type as i32;
                table.shapes.entry(key).or_default().paths.extend(paths);
                table.defaults.entry(key).or_insert(config); // keeps the first
            }
            Err(error) => {
                let message = format!("{name}/default-config.json: {error}");
                eprintln!("ConfigShape: skipping {message}");
                table.errors.push(message);
            }
        }
    }
    table
}

impl ConfigShape {
    /// Shape of a tag type, from its embedded default config(s).
    ///
    /// Built for all tag types on the first call, then cached. Two default
    /// configs with the same `tag_type` (PresTag and PresTagRaw) are
    /// unioned. Returns an empty shape for a tag type with no default.
    pub fn for_tag(tag_type: TagType) -> &'static ConfigShape {
        static EMPTY: ConfigShape = ConfigShape {
            paths: BTreeSet::new(),
        };
        TABLE.shapes.get(&(tag_type as i32)).unwrap_or(&EMPTY)
    }

    /// Shape of a JSON Config text: the keys it actually contains.
    ///
    /// Unlike parsing into Config, keys with zero values are kept, so a file
    /// that says `"period": 0` has `period` in its shape. Keys may use .proto
    /// or JSON (lowerCamelCase) names. Fails when the text is not a JSON
    /// object or names a field Config does not have.
    pub fn from_json(json: &str) -> Result<ConfigShape, String> {
        shape_of(json).map(|(paths, _)| ConfigShape { paths })
    }

    /// Shape of a configuration file: the fields it speaks for.
    ///
    /// qtmonitor's Save writes every Config field, padding the ones the tag
    /// does not have with zeros, so a file's keys over-state what it means.
    /// When the file names a `tag_type` that has a default config, its shape
    /// is [`from_json`](Self::from_json) limited to [`for_tag`](Self::for_tag)
    /// of that type. A file with no known `tag_type` is taken at its word.
    pub fn for_file(json: &str) -> Result<ConfigShape, String> {
        let (mut paths, tag_name) = shape_of(json)?;
        if let Some(tag_type) = parse_tag_type(&tag_name) {
            let own = &Self::for_tag(tag_type).paths;
            if !own.is_empty() {
                paths.retain(|p| own.contains(p));
            }
        }
        Ok(ConfigShape { paths })
    }

    /// True when `path` names a field of Config at all.
    pub fn is_config_path(path: &str) -> bool {
        VALID_PATHS.contains(path)
    }

    /// The default configuration of a tag type, as its `default-config.json`
    /// gives it; the first by directory name when two share a type.
    pub fn default_config(tag_type: TagType) -> Option<Config> {
        TABLE.defaults.get(&(tag_type as i32)).cloned()
    }

    /// Every tag type with a default config, in TagType order.
    pub fn tag_types() -> Vec<TagType> {
        TABLE
            .shapes
            .keys()
            .filter_map(|k| TagType::try_from(*k).ok())
            .collect()
    }

    /// One message per embedded default config that was skipped, naming the
    /// file and the reason; empty when every default loaded.
    pub fn load_errors() -> &'static [String] {
        &TABLE.errors
    }

    /// True when the shape includes the field at `path`.
    ///
    /// A path that names no Config field is a programming error, not an
    /// unsupported field: it panics in debug builds, and is reported on
    /// stderr and answered false otherwise.
    pub fn has(&self, path: &str) -> bool {
        if !Self::is_config_path(path) {
            eprintln!("ConfigShape::has(\"{path}\"): not a Config field");
            debug_assert!(false, "ConfigShape::has(\"{path}\"): not a Config field");
            return false;
        }
        self.paths.contains(path)
    }

    /// Every path in the shape, sorted.
    pub fn paths(&self) -> &BTreeSet<String> {
        &self.paths
    }

    /// Copies into `target` the fields that this shape and `overlay_shape`
    /// both include, taking their values from `overlay`.
    ///
    /// Scalars and enums are copied as they are, so a zero the overlay states
    /// is copied. Messages are merged field by field. The repeated
    /// `hibernate` is copied element by element, up to the smaller of the two
    /// counts. `tag_type` is never copied. Fields outside this shape are
    /// never added to `target`.
    pub fn merge(&self, target: &mut Config, overlay: &Config, overlay_shape: &ConfigShape) {
        let mut to = target.transcode_to_dynamic();
        let from = overlay.transcode_to_dynamic();
        let mut skip_under: Option<String> = None;
        for path in &self.paths {
            if let Some(prefix) = &skip_under
                && path.starts_with(prefix.as_str())
            {
                continue;
            }
            skip_under = None;
            if !overlay_shape.paths.contains(path) {
                continue;
            }
            let segs: Vec<&str> = path.split('.').collect();
            if merge_path(&mut to, &from, &segs) == Copied::List {
                skip_under = Some(format!("{path}."));
            }
        }
        *target = to
            .transcode_to()
            .expect("a Config transcodes back to Config");
    }
}

#[derive(PartialEq)]
enum Copied {
    /// A scalar was copied, or nothing could be.
    Field,
    /// The path ended at a repeated field, which was handled whole.
    List,
}

/// Walks `segs` through both messages, creating parents in `to`, and copies
/// the final field.
fn merge_path(to: &mut DynamicMessage, from: &DynamicMessage, segs: &[&str]) -> Copied {
    let Some(field) = to.descriptor().get_field_by_name(segs[0]) else {
        return Copied::Field;
    };
    if segs.len() > 1 {
        if field.is_list() {
            // Only reached for paths under a repeated field whose own path is
            // missing from one shape; nothing sensible to copy.
            return Copied::Field;
        }
        let from_child = from.get_field(&field);
        let (Value::Message(to_child), Value::Message(from_child)) =
            (to.get_field_mut(&field), from_child.as_ref())
        else {
            return Copied::Field;
        };
        return merge_path(to_child, from_child, &segs[1..]);
    }

    if field.is_list() {
        if field.kind().as_message().is_some() {
            let source = from.get_field(&field);
            if let (Value::List(dst), Value::List(src)) =
                (to.get_field_mut(&field), source.as_ref())
            {
                let n = dst.len().min(src.len());
                dst[..n].clone_from_slice(&src[..n]);
            }
        }
        return Copied::List;
    }
    if field.kind().as_message().is_none() {
        let value = from.get_field(&field).into_owned();
        to.set_field(&field, value);
    }
    Copied::Field
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::proto::{BitTagLogFmt, config};

    #[test]
    fn every_default_config_loads() {
        assert_eq!(ConfigShape::load_errors(), &[] as &[String]);
        assert!(ConfigShape::tag_types().contains(&TagType::BittagLe));
    }

    #[test]
    fn bittag_shape_lists_its_fields_and_not_others() {
        let shape = ConfigShape::for_tag(TagType::BittagLe);
        assert!(shape.has("bittag_log"));
        assert!(shape.has("adxl362.inactive_sec"));
        assert!(shape.has("hibernate.end_epoch"));
        assert!(!shape.has("period"));
        assert!(!shape.paths().contains("tag_type"));
    }

    #[test]
    fn file_shape_keeps_zeros_and_accepts_json_names() {
        let shape = ConfigShape::from_json(r#"{"period": 0, "startDelay": 0}"#).unwrap();
        assert!(shape.paths().contains("period"));
        assert!(shape.paths().contains("start_delay"));
        assert!(ConfigShape::from_json(r#"{"bogus": 1}"#).is_err());
        assert!(ConfigShape::from_json("[1]").is_err());
    }

    #[test]
    fn merge_copies_only_supported_fields_the_file_states() {
        let mut target = ConfigShape::default_config(TagType::BittagLe).unwrap();
        let text = r#"{"tag_type": "BITTAG_LE",
                       "bittag_log": "BITTAG_BITSPERMIN",
                       "adxl362": {"inact_thresh_g": 0},
                       "period": 30}"#;
        let overlay: Config = from_json(text).unwrap();
        let file_shape = ConfigShape::for_file(text).unwrap();
        ConfigShape::for_tag(TagType::BittagLe).merge(&mut target, &overlay, &file_shape);

        assert_eq!(target.bittag_log, BitTagLogFmt::BittagBitspermin as i32);
        let adxl = target.adxl362.unwrap();
        assert_eq!(adxl.inact_thresh_g, 0.0); // a stated zero is copied
        assert_eq!(adxl.act_thresh_g, 0.5); // an unstated field is kept
        assert_eq!(target.period, 0); // BitTag has no period
        assert_eq!(target.hibernate.len(), 2);
    }

    #[test]
    fn merge_copies_repeated_messages_elementwise() {
        let shape = ConfigShape::for_tag(TagType::BittagLe);
        let mut target = ConfigShape::default_config(TagType::BittagLe).unwrap();
        let text = r#"{"hibernate": [{"start_epoch": 5, "end_epoch": 6}]}"#;
        let overlay: Config = from_json(text).unwrap();
        shape.merge(&mut target, &overlay, &ConfigShape::for_file(text).unwrap());
        assert_eq!(
            target.hibernate,
            vec![
                config::Interval {
                    start_epoch: 5,
                    end_epoch: 6
                },
                config::Interval::default()
            ]
        );
    }
}
