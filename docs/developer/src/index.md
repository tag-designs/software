# Ultralight Tags Developer Docs

This site is the browser entry point for understanding and navigating the code.
It is separate from the end-user manuals under `host/docs`, which are packaged
with the host tools.

- [Developer Documentation Index](reference/docs/index.md): every developer
  document, grouped by the code that owns it.
- [Documentation Guide](reference/docs/documentation-guide.md): the document
  types, where each lives, and the front matter that drives this site.
- [API Reference](api.md): Doxygen output for project-owned C and C++ source.

The sidebar under **Source Tree** mirrors the repository and is generated from
each document's front matter. The pages are staged from their source locations
during the build; edit the original files in the repository, not the generated
build tree.
