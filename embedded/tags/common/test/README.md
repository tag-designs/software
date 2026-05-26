# Shared Self-Test Driver

The monitor-facing self-test dispatcher lives in `core/src/test.c` and is
compiled by the `tag_core` module. It asks the active tag or family `devices.c`
for a `TagTestCase` table, then maps protobuf `TestReq` values onto the
device-specific hook functions listed in that table. Each table entry can carry
a descriptor context pointer, so reusable tests can run against the concrete
device selected by the tag/family without adding a local wrapper only to close
over a global descriptor. Hooks return `ALL_PASSED` on success or a
device-specific `TestResult` on failure.

The `tag_test` module is retained as a no-op compatibility marker for older
project manifests that list it in `TAG_MODULES`.

Device tests should live beside the code that owns the hardware:

- reusable device module tests live with the module;
- family-specific device tests live in the family directory;
- one-off or legacy tests can stay tag-local.

The owning module or family manifest is responsible for:

- adding the hook source file;
- adding the hook and descriptor context to the tag/family `devices.c`
  `TagTestCase` table;
- returning `ALL_PASSED` or a specific `TestResult` from the hook;
- documenting the hook in `embedded/tags/README.md` and
  `CUSTOM_DEFINES.md` when a new capability is added.

Avoid replacing the shared dispatcher locally unless a tag needs a completely
different diagnostic entry point. Most new work should add a small hook and
reuse the shared dispatcher.
