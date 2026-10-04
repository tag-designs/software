---
type: readme
status: current
summary: How decision records are numbered, written and superseded.
---

# Decision Records

A decision record says what was chosen, what was rejected, and the evidence. It
is written once and then frozen: when a later decision replaces it, set its
status to `superseded` with `superseded-by:`, and leave the text alone.

Name records `NNNN-short-title.md`, numbered in the order written. Use this
shape:

```markdown
---
type: decision
status: accepted
summary: One sentence stating the decision.
---

# NNNN. Title stating the decision

Date: YYYY-MM-DD

## Context
## Decision
## Alternatives considered
## Evidence
## Consequences
```

Decisions that matter only inside one subsystem still go here, so there is one
numbered sequence to search. The design document for that subsystem links to
them.
