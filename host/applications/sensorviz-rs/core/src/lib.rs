//! sensorViz's engine, with no UI toolkit: what the Qt sensorviz's
//! `MainWindow` does besides drawing, over sensorViz's C++ core
//! (`sensorviz-session`), which the Qt app runs too.
//!
//! The [`engine`] owns the open log's session and the per-tag preferences,
//! sends the page its whole view state after every change, and answers the
//! page's queries for envelopes and the hover readout. The page (`../ui`)
//! draws it with uPlot.

pub mod engine;
