# Tag Common Build Modules

The tag firmware build still uses ChibiOS makefiles, but shared sources are now
grouped by module instead of being listed individually in every tag
`project.mk`.

Each tag declares:

```make
TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       rtc_rv3028

include ../common/modules/modules.mk
```

The module fragments in this directory append source basenames to `ALLCSRC`.
They deliberately do not use full paths. The ChibiOS makefile's `VPATH` searches
the tag-local `src` directory before `../common/src`, so a tag can override a
shared default by providing a same-named local source file. Likewise, tag-local
headers in `inc` are searched before `../common/inc`.

This depends on `../make.mk` repairing the order after ChibiOS `rules.mk` is
included. ChibiOS sorts its generated include flags and source paths; the common
tag makefile rebuilds `IINCDIR` and `VPATH` so local override directories stay
ahead of shared defaults.

The physical shared sources still live in `../common/src` and headers in
`../common/inc`; this is a build-manifest organization step, not a source move.

When adding a shared source file:

- add it to the narrowest module that owns the behavior;
- create a new module if no existing group fits cleanly;
- keep tag-local application code listed directly in that tag's `project.mk`.
