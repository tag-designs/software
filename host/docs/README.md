# Documentation Template

This directory is a starter for writing package-distributed Qt application
guides with MkDocs Material. It lives under `host` because these docs
ship with the host applications rather than the project-level documentation.

## Preview Locally

Install the documentation dependencies:

```sh
python3 -m pip install -r host/docs/requirements.txt
```

Run the local preview server:

```sh
python -m mkdocs serve -f host/docs/mkdocs.yml
```

Build static HTML:

```sh
python -m mkdocs build -f host/docs/mkdocs.yml
```

The generated site is written to `host/docs/site/`.

You can also build the same site through CMake from any top-level build
directory:

```sh
cmake --build build --target docs
```

With Makefile generators, this is equivalent to running `make docs` from the
build directory. The generated CMake-built site is written under the build tree
at `host/docs/site/`.

## Package Build

The CMake `docs` target is available in top-level builds. The
`BUILD_HOST_DOCS` option controls whether that target is part of the default
build and whether generated docs are installed into packages. By default, that
option follows `BUILD_QT_APPS`.

CMake first looks for `mkdocs` on `PATH`, then falls back to `Python3 -m
mkdocs`. If your Python install works as a module but CMake cannot auto-detect
it, configure with a semicolon-separated command list:

```sh
cmake -S . -B build -DHOST_MKDOCS_COMMAND="python;-m;mkdocs"
```

Generated package docs are installed alongside the host tools:

- Windows: `tag_tools/docs`
- macOS: `tag_tools/docs`
- Linux: `share/UltralightTags/docs`

## Source Layout

Markdown source files live in `host/docs/src/`. Images should go in
`host/docs/src/images/` and be referenced with relative paths such as:

```md
![Qt Program main window](../images/qtprogram-main-window.png)
```
