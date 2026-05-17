# Tag Families

Some firmware targets are build variants of the same tag design.  A common
example is a production tag and a breakout tag that use different generated
board files but should share most application code.

Family directories hold the code that should stay identical across those
variants.  Variant directories remain responsible for selecting the board,
modules, ChibiOS configuration, and any local overrides needed during bring-up.

The common tag makefile searches paths in this order:

1. variant `inc/` and `src/`
2. family `inc/` and `src/`
3. selected common modules
4. legacy common directories and generated code

This preserves the existing override model: during breakout testing, a variant
can place a same-named file in its local `inc/` or `src/` directory to override
the family copy.  Keep those overrides temporary and visible in `project.mk` so
unintended divergence does not become permanent.
