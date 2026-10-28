---
type: decision
status: accepted
summary: The host web front ends render with Preact and htm, vendored as classic scripts in host/ui-kit with shared components, keeping the kit's macOS look and the no-build, open-from-disk workflow; Pico CSS was declined.
---

# 0038. Host: the UI kit renders with Preact

Date: 2026-10-28

## Context

[0035](0035-host-tagprogram-rs.md) built Tag Programmer's window as a plain
web page on `host/ui-kit`, with no front-end framework or bundler, and Tag
Monitor ([0037](0037-host-tagmonitor-rs.md)) followed. Each page then drew
its view state by hand: about 450 to 500 lines of DOM code per app, keeping
controls in step with the state, with nothing shared between the two apps
but the kit's CSS and dialogs. Tag Monitor's configuration form had grown its
own rebuild-or-update logic to keep a field's focus while it is typed into.

The user asked whether a lightweight toolkit would make the pages easier to
maintain and give the tools a common style, and suggested Preact, perhaps
with Pico CSS.

## Decision

- **Render with Preact 10 through htm.** Each page renders its whole window
  from every view state, and Preact changes only what differs. This matches
  the engine contract, which already sends the whole state after each change.
- **Vendor, no build step.** `host/ui-kit/preact-htm.js` is Preact's, its
  hooks' and htm's published UMD builds joined in one classic script. Pages
  still open from `file://` against their mock engines, and CMake, CI and
  `build.rs` (which stages the whole kit directory) need no change.
- **Shared components in the kit.** `kit-components.js` draws the kit's own
  classes: buttons, popups, group boxes, form rows, progress bars, status
  rows and the log drawer. `Kit.dialog` also accepts a Preact body.
- **Keep the kit's look.** `kit.css` stays the style of every host web page,
  matching the Qt applications on macOS, which ship alongside them.
- **Port one app at a time, held to the old page.** Tag Programmer first.

## Alternatives considered

- **Pico CSS.** It would replace the kit's look rather than add to it: Pico
  styles plain HTML in its own web idiom, so the Rust apps would stop
  matching qtmonitor, sensorviz and qtcalibrate. Layered under `kit.css` it
  would mean two stylesheets fighting. The user chose to keep the kit.
- **Lit (web components).** Also small and buildless, with better isolation
  for shared elements, but more ceremony than a page that redraws from one
  state needs.
- **Svelte, Solid, React or Vue.** These need a Node build, and so npm in
  CMake, CI and the release, for two small windows.
- **Shoelace or Web Awesome.** Polished controls, but much heavier, and not
  the native look.
- **Keep hand-written DOM code.** It works, but each new panel or app
  repeats the synchronisation the components now do once.

## Evidence

- **Same pixels.** 30 headless-Chrome screenshots of Tag Programmer (12 mock
  scenarios in light and dark, the log drawer and the Program dialog) are
  identical before and after the port. The one region that differed, the
  indeterminate progress animation, also differs between repeated captures
  of one build.
- **Same behaviour.** A scripted run through Program (with the label
  conflict), Retest, Sync, Erase, Stop, both popups, the firmware file
  picker, the log and the Station menu gives the same commands and dialogs
  against the old page and the new; only the log level popup's element id
  changed.
- **Size.** `preact-htm.js` is about 17 KB. Tag Programmer's `index.html`
  shrank to the window chrome, and `app.js` reads as the window's layout.
- **In the app.** The user ran the rebuilt Tag Programmer and found it
  "works great".

## Consequences

- Tag Monitor's page is ported next; its configuration form is expected to
  gain most.
- Preact and htm are third-party code in the shipped apps, listed in
  `LICENSES/host/`. Updating them is a documented manual step
  ([kit README](../../host/ui-kit/README.md#updating-preact-and-htm)).
- 0035's "no front-end framework" no longer holds; its other decisions
  stand.
