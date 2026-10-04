---
type: readme
status: current
summary: Where dated fault investigations and measurement campaigns are kept, and how they are closed.
---

# Investigations

An investigation is the dated record of a fault chased or a question measured:
what was tried, what was measured, what was excluded. It is append-only while
open. When it ends, set `status: closed`, state the outcome in its first
paragraph, and link the decision record or commit that resolved it.

Investigations are kept rather than archived. They hold the evidence behind
the rules in the design documents, and the mechanisms already excluded, so the
next person chasing a similar fault starts from them.

Cross-cutting investigations live here as `YYYY-MM-short-title.md`. One that
belongs to a single subsystem lives in that subsystem's `design/investigations/`.
