# Developer Documentation

This directory defines the developer documentation portal. It is separate from
the end-user manuals under `host/docs`, which are packaged with host
applications.

Build from a configured CMake tree:

```sh
cmake --build build-host --target developer_docs
```

The generated browser entry point is:

```text
build-host/docs/developer/site/index.html
```

Useful related targets:

```sh
cmake --build build-host --target developer_docs_stage
cmake --build build-host --target api_docs
cmake --build build-host --target all_docs
```

`developer_docs_stage` copies curated Markdown files from their source
locations into the build tree under `reference/`. `api_docs` runs Doxygen when
it is available. `developer_docs` builds the MkDocs portal and, when Doxygen is
available, places the API reference under `api/`.

When adding source design documents that should appear in the portal, update:

- the nearest local README or design index;
- top-level `design/index.md` when the document has cross-subsystem value;
- `DEVELOPER_DOCS_MARKDOWN` in top-level `CMakeLists.txt`;
- `docs/developer/mkdocs.yml`.
