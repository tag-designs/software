# Tag Designs Host Tools: Licenses

The host tools are Copyright (c) 2018-2026 The Trustees of Indiana University
and licensed under the [MIT License](LICENSE).

They include the third-party software listed below, each used under its own
license. Every component has a notice file linked in the first column. That
file gives its copyright, where it is used and where its source is, and holds
its full license text or names the shared text in [texts/](texts/).

## Programs distributed under the GNU GPL, version 3

Three programs contain components licensed only under the GPL, so as
distributed they are licensed under the
[GNU General Public License, version 3](texts/GPL-3.0-only.txt):

| Program | Because it contains |
| --- | --- |
| sensorviz, btviz | [QCustomPlot](third-party/qcustomplot.txt) (GPL-3.0-or-later) |
| qtcalibrate | Qt Quick 3D and Qt Quick Timeline from [Qt](third-party/qt.txt) (GPL-3.0-only) |

The project's own code in them remains available under the MIT License.

**Source code.** The complete corresponding source of every program is the
Tag Designs software repository, https://github.com/tag-designs/software, at
the release tag the package was built from (`vX.Y`). It includes the
third-party sources it names: vcpkg ports at the baseline in
`vcpkg-configuration.json`, and Qt 6.10.3 from
https://download.qt.io/archive/qt/6.10/6.10.3/single/. This offer is valid for
at least three years from the release date, for anyone who receives the
programs.

## Third-party software

### Libraries linked into the programs

| Component | Version | License | Used in |
| --- | --- | --- | --- |
| [Qt](third-party/qt.txt) | 6.10.3 | [LGPL-3.0-only](texts/LGPL-3.0-only.txt); Quick 3D, Quick 3D Physics, Quick Timeline, Virtual Keyboard [GPL-3.0-only](texts/GPL-3.0-only.txt) | Qt applications (shared libraries) |
| [Third-party code inside Qt](Qt-THIRD-PARTY-NOTICES.txt) | — | various, listed in the file | Qt applications |
| [QCustomPlot](third-party/qcustomplot.txt) | 2.1.1 | [GPL-3.0-or-later](texts/GPL-3.0-only.txt) | sensorviz, btviz |
| [libusb](third-party/libusb.txt) | 1.0.29 | [LGPL-2.1-or-later](texts/LGPL-2.1-or-later.txt) | programs that talk to a tag |
| [Protocol Buffers](third-party/protobuf.txt) | 6.33.4 | BSD-3-Clause | all programs |
| [Abseil](third-party/abseil.txt) | 20260107.1 | [Apache-2.0](texts/Apache-2.0.txt) | all programs |
| [utf8_range](third-party/utf8_range.txt) | 6.33.4 | MIT | all programs |
| [SQLite](third-party/sqlite.txt) | 3.53.1 | public domain | programs that read or write logs |
| [cxxopts](third-party/cxxopts.txt) | 3.0.0 | MIT | command line tools |
| [log.c (rxi)](third-party/rxi-log.txt) | 0.1.0 | MIT | all programs |
| [stlink commands.h](third-party/stlink-commands.txt) | — | BSD-3-Clause | programs that talk to a tag |
| [Freescale magnetic calibration](third-party/freescale-magcal.txt) | — | BSD-3-Clause | qtcalibrate |
| [MotionCal magcal.h](third-party/motioncal-quality.txt) | — | none found | qtcalibrate |
| [Kiss FFT](third-party/kissfft.txt) | 1.3.0 | BSD-3-Clause | btviz |
| [FastFIR](third-party/fastfir.txt) | — | MIT | btviz |
| [SOLPOS (NLR, formerly NREL)](third-party/nrel-solpos.txt) | 2.0 | NLR data and software notice | btviz |
| [QFlightInstruments artwork](third-party/qflightinstruments.txt) | — | MIT | sensorviz, qtcalibrate |

### User manual (`docs/`)

| Component | Version | License | Used for |
| --- | --- | --- | --- |
| [Material for MkDocs](third-party/mkdocs-material.txt) | 9.7.6 | MIT | theme |
| [RxJS](third-party/rxjs.txt) | — | [Apache-2.0](texts/Apache-2.0.txt) | theme |
| [clipboard.js](third-party/clipboard-js.txt) | — | MIT | theme |
| [escape-html](third-party/escape-html.txt) | — | MIT | theme |
| [focus-visible](third-party/focus-visible.txt) | — | W3C-20150513 | theme |
| [tslib](third-party/tslib.txt) | — | 0BSD | theme |
| [Material Design Icons](third-party/material-design-icons.txt) | — | [Apache-2.0](texts/Apache-2.0.txt) | icons |
| [Font Awesome Free icons](third-party/font-awesome.txt) | 7.1.0 | [CC-BY-4.0](texts/CC-BY-4.0.txt) | icons |
| [Lunr](third-party/lunr.txt) | 2.3.9 | MIT | search |
| [lunr-languages](third-party/lunr-languages.txt) | — | [MPL-1.1](texts/MPL-1.1.txt) | search |
| [wordcut](third-party/wordcut.txt) | — | [LGPL-3.0](texts/LGPL-3.0-only.txt) | search |
| [TinySegmenter](third-party/tinysegmenter.txt) | 0.1 | BSD-3-Clause | search |

### Windows runtime

| Component | License | Used in |
| --- | --- | --- |
| [Visual C++ and Universal C runtimes, D3Dcompiler](third-party/microsoft-runtime.txt) | Microsoft redistributable terms | Windows package |

### Firmware loaders

The external flash loaders installed with some builds of the tools are
firmware. Their licenses are listed in the firmware license directory,
installed beside the loaders in the `licenses/` directory next to `loaders/`.

## License texts

| Text | File |
| --- | --- |
| MIT License (this project) | [LICENSE](LICENSE) |
| GNU General Public License v3 | [texts/GPL-3.0-only.txt](texts/GPL-3.0-only.txt) |
| GNU Lesser General Public License v3 | [texts/LGPL-3.0-only.txt](texts/LGPL-3.0-only.txt) |
| GNU Lesser General Public License v2.1 | [texts/LGPL-2.1-or-later.txt](texts/LGPL-2.1-or-later.txt) |
| Apache License 2.0 | [texts/Apache-2.0.txt](texts/Apache-2.0.txt) |
| Mozilla Public License 1.1 | [texts/MPL-1.1.txt](texts/MPL-1.1.txt) |
| Creative Commons Attribution 4.0 | [texts/CC-BY-4.0.txt](texts/CC-BY-4.0.txt) |

The MIT, BSD, Zlib, 0BSD and W3C licenses and the NLR notice are reproduced in full, with their
copyright notices, in each component's file.
