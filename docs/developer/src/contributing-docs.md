# Documentation Maintenance

Developer documentation has two source forms:

- Doxygen comments beside APIs and important internal contracts.
- Markdown design notes and READMEs beside the systems they explain.

When adding, moving, renaming, or materially changing a developer design note:

- Update the nearest local `README.md` or local `design/index.md`.
- Update top-level `design/index.md` when the note has cross-subsystem value.
- Update the developer-doc CMake manifest so the browser portal stages it.
- Update the source-tree navigation in `docs/developer/src/source-tree.md`.
- Add a curated topic link in `docs/developer/mkdocs.yml` only when the note is
  a useful entry point, not merely because it exists.
- Keep historical or exploratory notes clearly marked instead of mixing them
  into active architecture sections.

The generated build tree is only a rendered portal. Edit the source Markdown or
source comments in the repository.
