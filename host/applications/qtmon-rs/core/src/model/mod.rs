//! The rules, as plain Rust over tagcore-rs's protobuf types.
//!
//! | Module | Ports | Holds |
//! | --- | --- | --- |
//! | [`commands`] | `core/commands.h` | The commands and the one rule for when each is available |
//! | [`startchecks`] | `core/startchecks.h` | The checks Start makes, and their sentences |
//! | [`tagrules`] | `core/tagrules.h` | Downloadable states, erase size, clock offset, version floor, base names |
//! | [`monitormodel`] | `core/monitormodel.h` | The attached tag as known, and what each status settles |
//! | [`configdocument`] | `core/configdocument.h` | The configuration document, its file format and approval |
//! | [`menus`] | `core/menus.h` | The menus as data |

pub mod commands;
pub mod configdocument;
pub mod configeditor;
pub mod configform;
pub mod menus;
pub mod monitormodel;
pub mod startchecks;
pub mod tagrules;
