---
type: readme
status: current
summary: How to build and run qtprogram_checks, the offline checks of the CLI parsers, the record CSV, the release-archive member filter, the release list, and the download-extract-verify path.
---

# qtprogram Offline Checks

`qtprogram_checks` is a plain `main()` with assertions that runs qtprogram's
station pieces with no tag, base or network. As elsewhere in this tree, there
is no test framework to register it with, so build it explicitly:

```sh
cmake -DBUILD_QTPROGRAM_CHECKS=ON <build-dir>
cmake --build <build-dir> --target qtprogram_checks
<build-dir>/<config>/bin/qtprogram_checks [tag-firmware-fw-vX.Y.tar.gz]
```

It prints "All checks passed", or one `FAIL:` line per failure and exits 1.

| Area | What is checked |
| --- | --- |
| CLI parsers | Version, probe list and SWD connect, against output captured from STM32CubeProgrammer v2.22.0 on the bench base (colour escapes included), plus a failed connect and an exit 0 without a device ID |
| Record CSV | Quoting and parsing (commas, quotes and line breaks in fields), create, append, reopen, label and UUID lookups, column order, a refused foreign CSV, and that a refused open keeps the record already open |
| Archive members | Only `firmware/<T>/<T>.elf` and its manifest are accepted; paths with `..`, absolute paths, other files and nested directories are not |
| Release list | Drafts, host releases and releases without a firmware archive are dropped; newest version first (`fw-v0.10` after `fw-v0.7`); the qualified targets read from `qualification-<Target>[-<date>].tar.gz` asset names, and an unlisted release never counts as qualified |
| Fetch | Given a release archive: download through a `file://` URL, extraction with the system `tar`, manifest verification, the cache, identity records, and the refusal of a cached image changed afterwards. A release before `fw-v0.7` (no flash layout symbol) must still cache, with each image refused for that reason |

Get an archive with:

```sh
gh release download fw-v0.7 -R tag-designs/software -p 'tag-firmware-*'
```
