# API Reference

Doxygen reference for the project-owned, non-generated C and C++ source. The
Rust crates have their own, the [Rust Reference](rust.md). Use it
for symbol contracts; use the Markdown design documents for architecture,
rationale and workflows.

| Reference | Covers |
| --- | --- |
| [Libraries and firmware](api/index.md) | `include/`, tagcore, sensoranalysis, sensorui, `host/common`, `embedded/boards`, `embedded/loaders`, `embedded/tags/common` and `families` |
| [Tag Monitor (qtmonitor)](api/qtmonitor/index.md) | `host/applications/qtmon` |
| [Tag Programmer (qtprogram)](api/qtprogram/index.md) | `host/applications/qtprogram` |
| [Tag Calibrator (qtcalibrate)](api/qtcalibrate/index.md) | `host/applications/qtcalibrate` |
| [SensorViz (sensorviz)](api/sensorviz/index.md) | `host/applications/sensorviz` |
| [Command-line tools](api/commandline/index.md) | `host/commandline` |

Each application has its own reference because several define classes of the
same name (`MainWindow`, `LogWindow`); one reference would merge them. An
application's links to library symbols, such as `Tag` or `ConfigShape`, lead to
the library reference. The pages are built by the `api_docs` and
`developer_docs` targets; the list of applications is `DEVELOPER_DOXYGEN_APPS`
in the top-level `CMakeLists.txt`.
