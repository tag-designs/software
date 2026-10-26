//! Protobuf text format as the C++ library prints it.
//!
//! `prost_reflect`'s own text format writes repeated fields as lists; the
//! C++ `TextFormat` (and so `DebugString()`, which the tools print) repeats
//! the field name per element. This printer follows the C++ rules, so a
//! Rust tool's output reads the same as its C++ counterpart:
//!
//! - set fields only, in field-number order (proto3 scalars at their default
//!   are unset unless they have explicit presence or are a oneof member);
//! - messages as `name {` ... `}`; repeated fields one element per entry;
//! - enums by name, or by number when the value has no name;
//! - floats and doubles in `%g` style with the C++ precision rules;
//! - strings with C escapes for quotes, backslash and control bytes.
//!
//! The C++ library also inserts a randomised marker into `DebugString()`
//! output in recent releases; that is deliberately not reproduced.

use std::fmt::Write as _;

use prost_reflect::{DynamicMessage, FieldDescriptor, Kind, ReflectMessage, Value};

/// Multi-line text format, as `Message::DebugString()` prints it.
pub fn debug_string<M: ReflectMessage>(msg: &M) -> String {
    let mut out = String::new();
    print_message(&msg.transcode_to_dynamic(), false, 0, &mut out);
    out
}

/// Single-line text format, as `Message::ShortDebugString()` prints it.
pub fn short_debug_string<M: ReflectMessage>(msg: &M) -> String {
    let mut out = String::new();
    print_message(&msg.transcode_to_dynamic(), true, 0, &mut out);
    out.trim_end().to_owned()
}

/// Appends every set field of `msg`. Single-line output separates fields
/// with a space; multi-line output ends each with a newline.
fn print_message(msg: &DynamicMessage, single_line: bool, indent: usize, out: &mut String) {
    let mut fields: Vec<(FieldDescriptor, &Value)> = msg.fields().collect();
    fields.sort_by_key(|(f, _)| f.number());
    for (field, value) in fields {
        match value {
            Value::List(items) => {
                for item in items {
                    print_field(&field, item, single_line, indent, out);
                }
            }
            Value::Map(_) => {} // tag.proto has no map fields
            v => print_field(&field, v, single_line, indent, out),
        }
    }
}

fn print_field(
    field: &FieldDescriptor,
    value: &Value,
    single_line: bool,
    indent: usize,
    out: &mut String,
) {
    if !single_line {
        out.push_str(&"  ".repeat(indent));
    }
    out.push_str(field.name());
    match value {
        Value::Message(m) => {
            if single_line {
                out.push_str(" { ");
                print_message(m, true, 0, out);
                out.push_str("} ");
            } else {
                out.push_str(" {\n");
                print_message(m, false, indent + 1, out);
                out.push_str(&"  ".repeat(indent));
                out.push_str("}\n");
            }
            return;
        }
        v => {
            out.push_str(": ");
            out.push_str(&scalar(field, v));
        }
    }
    out.push(if single_line { ' ' } else { '\n' });
}

/// One scalar or enum value as C++ text format writes it.
pub fn scalar(field: &FieldDescriptor, value: &Value) -> String {
    match value {
        Value::Bool(b) => b.to_string(),
        Value::I32(v) => v.to_string(),
        Value::I64(v) => v.to_string(),
        Value::U32(v) => v.to_string(),
        Value::U64(v) => v.to_string(),
        Value::F32(v) => format_g(f64::from(*v), 6, 9, |s| s.parse::<f32>().ok() == Some(*v)),
        Value::F64(v) => format_g(*v, 15, 17, |s| s.parse::<f64>().ok() == Some(*v)),
        Value::String(s) => format!("\"{}\"", c_escape(s.as_bytes())),
        Value::Bytes(b) => format!("\"{}\"", c_escape(b)),
        Value::EnumNumber(n) => match field.kind() {
            Kind::Enum(e) => e
                .get_value(*n)
                .map(|v| v.name().to_owned())
                .unwrap_or_else(|| n.to_string()),
            _ => n.to_string(),
        },
        Value::Message(m) => short_debug_dynamic(m),
        Value::List(_) | Value::Map(_) => String::new(),
    }
}

fn short_debug_dynamic(m: &DynamicMessage) -> String {
    let mut out = String::new();
    print_message(m, true, 0, &mut out);
    out.trim_end().to_owned()
}

/// `%.{short}g`, falling back to `%.{long}g` when that does not round-trip:
/// the C++ `SimpleFtoa`/`SimpleDtoa` rule. Non-finite values print as
/// `inf`, `-inf` and `nan`.
pub fn format_g(v: f64, short: usize, long: usize, round_trips: impl Fn(&str) -> bool) -> String {
    if v.is_nan() {
        return "nan".into();
    }
    if v.is_infinite() {
        return if v > 0.0 { "inf".into() } else { "-inf".into() };
    }
    let s = percent_g(v, short);
    if round_trips(&s) {
        s
    } else {
        percent_g(v, long)
    }
}

/// C's `%.{precision}g`.
fn percent_g(v: f64, precision: usize) -> String {
    if v == 0.0 {
        return if v.is_sign_negative() {
            "-0".into()
        } else {
            "0".into()
        };
    }
    let p = precision.max(1);
    // Round to p significant digits first; the exponent of the rounded value
    // decides the layout.
    let sci = format!("{:.*e}", p - 1, v);
    let (mantissa, exp) = sci.split_once('e').unwrap();
    let exp: i32 = exp.parse().unwrap();
    if exp < -4 || exp >= p as i32 {
        let mantissa = strip_zeros(mantissa);
        format!(
            "{mantissa}e{}{:02}",
            if exp < 0 { '-' } else { '+' },
            exp.abs()
        )
    } else {
        let decimals = (p as i32 - 1 - exp).max(0) as usize;
        strip_zeros(&format!("{v:.decimals$}")).to_owned()
    }
}

fn strip_zeros(s: &str) -> &str {
    if s.contains('.') {
        s.trim_end_matches('0').trim_end_matches('.')
    } else {
        s
    }
}

/// C escaping as the C++ text format applies it: quotes, backslash and the
/// usual control characters by name, other control bytes in octal, and
/// everything else (UTF-8 included) as is.
fn c_escape(bytes: &[u8]) -> String {
    let mut out = String::new();
    let text = String::from_utf8_lossy(bytes);
    for c in text.chars() {
        match c {
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            '"' => out.push_str("\\\""),
            '\'' => out.push_str("\\'"),
            '\\' => out.push_str("\\\\"),
            c if (c as u32) < 0x20 || c as u32 == 0x7f => {
                write!(out, "\\{:03o}", c as u32).unwrap();
            }
            c => out.push(c),
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    fn f32g(v: f32) -> String {
        format_g(f64::from(v), 6, 9, |s| s.parse::<f32>().ok() == Some(v))
    }

    #[test]
    fn floats_follow_simple_ftoa() {
        assert_eq!(f32g(0.5), "0.5");
        assert_eq!(f32g(0.35), "0.35");
        assert_eq!(f32g(3.0), "3");
        assert_eq!(f32g(1.5), "1.5");
        assert_eq!(f32g(23.9), "23.9");
        assert_eq!(f32g(1e10), "1e+10");
        assert_eq!(f32g(0.0001), "0.0001");
        assert_eq!(f32g(0.00001), "1e-05");
        assert_eq!(f32g(-0.0), "-0");
        // Needs more than six digits, so nine are printed, as %.9g does.
        assert_eq!(f32g(1.2345678), "1.23456776");
        assert_eq!(f32g(f32::INFINITY), "inf");
    }

    #[test]
    fn strings_are_c_escaped() {
        assert_eq!(c_escape(b"a\"b\\c\n\x01"), "a\\\"b\\\\c\\n\\001");
        assert_eq!(c_escape("µT".as_bytes()), "µT");
    }
}
