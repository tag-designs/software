//! Tag Calibrator's engine, with no UI toolkit: what qtcalibrate's
//! `MainWindow` did besides drawing, over the C++ calibration session
//! (`calibsession`) that qtcalibrate's `CompassData` runs.
//!
//! The [`engine`] owns the tag (on its own thread), the calibration session,
//! the sample capture and the display settings, and sends the page its whole
//! view state after every change. The page (`../ui`) only draws it; the view
//! state and the commands are described in `ui/mock-engine.js` until the
//! contract has a design document of its own.

pub mod engine;
