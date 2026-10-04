---
type: readme
status: current
summary: What tag families are and the variant, family, module search order that lets variants override family files.
---

# Tag Families

Some firmware targets are build variants of the same tag design.  A common
example is a production tag and a breakout tag that use different generated
board files but should share most application code.
Another example is `BitPresTag` versus `BitPresTagMX25R`, where the application
code is shared and only the external flash module differs.

Family directories hold the code and ChibiOS configuration that should stay
identical across those variants.  Variant directories remain responsible for
selecting the board, modules, and any local overrides needed during bring-up.

The common tag makefile searches paths in this order:

1. variant `cfg/`, `inc/`, and `src/`
2. family `cfg/`, `inc/`, and `src/`
3. selected common modules
4. legacy common directories and generated code

This preserves the existing override model: during breakout testing, a variant
can place a same-named file in its local `cfg/`, `inc/`, or `src/` directory to
override the family copy.  Keep those overrides temporary and visible in
`project.mk` so unintended divergence does not become permanent.

Some overrides are permanent by design: `UIUCTag` replaces the BitPresTag
family's `state_run.c`, `datalog.[ch]`, `sensors.[ch]` and `devices.[ch]`
because it stores a different log record. A family README says which of its
variants have forked this way and why, so the next reader does not merge them
back. Each family's `README.md` lists what it shares and what its variants
keep local.
