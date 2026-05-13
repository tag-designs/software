# Documentation Template

This directory is a starter for writing package-distributed Qt application
guides with MkDocs Material. It lives under `software/host` because these docs
ship with the host applications rather than the project-level documentation.

## Preview Locally

Install the documentation dependencies:

```sh
python3 -m pip install -r requirements.txt
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

## Source Layout

Markdown source files live in `software/host/docs/src/`. Images should go in
`software/host/docs/src/images/` and be referenced with relative paths such as:

```md
![Qt Program main window](../images/qtprogram-main-window.png)
```
