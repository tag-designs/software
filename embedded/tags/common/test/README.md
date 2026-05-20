# Shared Self-Test Driver

The monitor-facing self-test dispatcher lives in `core/src/test.c` and is
compiled by the `tag_core` module. It maps protobuf `TestReq` values onto
device-specific hook functions declared in `core/inc/test_support.h`.

The `tag_test` module is retained as a no-op compatibility marker for older
project manifests that list it in `TAG_MODULES`.

Device tests should live beside the code that owns the hardware:

- reusable device module tests live with the module;
- family-specific device tests live in the family directory;
- one-off or legacy tests can stay tag-local.

The owning module or family manifest is responsible for:

- defining the `TAG_*` switch used by `test_support.h`;
- adding the hook source file;
- documenting the hook in `embedded/tags/README.md` and
  `CUSTOM_DEFINES.md` when a new capability is added.

Avoid replacing the shared dispatcher locally unless a tag needs a completely
different diagnostic entry point. Most new work should add a small hook and
reuse the shared dispatcher.
