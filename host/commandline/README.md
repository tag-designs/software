# Command-Line Tools

Command-line host tools live here. They link the Qt-free `tagcore` target from
`../libraries/tagcore`.

Current tools:

- `tag-dwnld`: download tag logs using the shared tag log writer interface.
- `tag-info`: inspect tag/base information.
- `tag-reset`: reset a tag.
- `tag-start`: start logging.
- `tag-cal`: calibration helper.
- `tag-test`, `tag-test-example`, `tag-monitor-test`: developer/test tools.

Keep command-line behavior independent of Qt so these tools remain lightweight
and usable in scripts.
