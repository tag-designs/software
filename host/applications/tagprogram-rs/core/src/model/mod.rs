//! Pure logic: images, flash plans, releases, the record and settings.
//!
//! Ports of qtprogram's model classes, with their rules and messages. Nothing
//! here touches USB or a UI toolkit; the engine (`crate::engine`) drives it.
//!
//! - [`elf`]: minimal ELF32 reader (segments, symbols).
//! - [`flashplan`]: which internal flash an image's programming erases.
//! - [`identity`]: the tag identity record an image carries.
//! - [`image`]: a firmware image, hashed and checked against its manifest.
//! - [`catalog`]: published firmware releases and the local image cache.
//! - [`record`]: the CSV record of programmed tags.
//! - [`settings`]: persisted choices.

pub mod catalog;
pub mod elf;
pub mod flashplan;
pub mod identity;
pub mod image;
pub mod record;
pub mod settings;
