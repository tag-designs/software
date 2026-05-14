# Documentation Template

This directory is a starter for writing package-distributed Qt application
guides with MkDocs Material. It lives under `software/host` because these docs
ship with the host applications rather than the project-level documentation.

## Preview Locally

Install the documentation dependencies:

```sh
python3 -m pip install -r software/host/docs/requirements.txt
```

Run the local preview server:

```sh
mkdocs serve -f software/host/docs/mkdocs.yml
```

Build static HTML:

```sh
mkdocs build -f software/host/docs/mkdocs.yml
```

The generated site is written to `software/host/docs/site/`.

## Package Build

The top-level CMake build includes these docs when `BUILD_HOST_DOCS=ON`. By
default, that option follows `BUILD_QT_APPS`. CMake expects the `mkdocs`
command to be on `PATH`; install the tools listed in `requirements.txt` before
configuring a package build.

Generated package docs are installed alongside the host tools:

- Windows: `tag_tools/docs`
- macOS: `tag_tools/docs`
- Linux: `share/UltralightTags/docs`

## Source Layout

Markdown source files live in `software/host/docs/src/`. Images should go in
`software/host/docs/src/images/` and be referenced with relative paths such as:

```md
![Qt Program main window](../images/qtprogram-main-window.png)
```
