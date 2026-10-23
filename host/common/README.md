---
type: readme
status: current
summary: Small shared Qt helpers used by host applications through the host_common target.
---

# Host Common Qt Helpers

This directory contains small headers/sources shared by Qt applications but not
large enough to justify a library of their own.

Current files:

- `qtfiledialog.h`: shared host file-dialog helper.
- `qtuserguide.h`: `HostUserGuide::open()`, which finds an application's page
  of the user manual (packaged beside the application, in the build tree, or
  built in the source tree) and opens it; used by every Help menu. The
  `host_common` target defines `TAG_DESIGNS_DOCS_BUILD_DIR` and
  `TAG_DESIGNS_DOCS_SOURCE_DIR` for it.
- `qthoststyle.h`: shared visual style setup for Qt applications.
- `windows_tool_launcher.cpp`: Windows package launcher source used by
  `host/CMakeLists.txt`.

Applications get this include path through the `host_common` CMake interface
target.
