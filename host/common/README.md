# Host Common Qt Helpers

This directory contains small headers/sources shared by Qt applications but not
large enough to justify a library of their own.

Current files:

- `qtfiledialog.h`: shared host file-dialog helper.
- `qthoststyle.h`: shared visual style setup for Qt applications.
- `windows_tool_launcher.cpp`: Windows package launcher source used by
  `host/CMakeLists.txt`.

Applications get this include path through the `host_common` CMake interface
target.
