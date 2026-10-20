# Maintain these docs

The QtProbe website uses the same VitePress layout, institutional links, local
search, light/dark appearance and build workflow as Qt StripTool, QtALH and
QtEDM. The validated static output is `docs/html/`.

## Build and preview

Use Node.js 22 or newer with npm, and Python 3. Qt and EPICS are not required.
From the repository root:

```sh
make docs
python3 -m http.server 8000 --directory docs/html --bind 127.0.0.1
```

Open `http://127.0.0.1:8000/`. Serve the directory over HTTP so the website's
modules and local search load correctly.

`make docs` refreshes imported pages and screenshots, installs locked npm
dependencies when absent, builds the website, checks page titles, local links,
assets, anchors and guide coverage, and copies the validated result to
`docs/html/`. For a clean dependency install, run `npm ci --prefix docs/site`
first.

## Host in the APS manuals directory

Build with the exact URL prefix, including its leading and trailing slashes:

```sh
make docs DOCS_BASE=/manuals/QtProbe/
```

Copy the complete contents of `docs/html/` to
`/home/oxygen/OAG/oagWWW/manuals/QtProbe/`. Configure the server to serve
`index.html` as the directory index. Keep the `assets/`, `images/`, `downloads/`
and page subdirectories together. Copying only `index.html` leaves out styles,
scripts, images and search data. Rebuild and copy all files when the prefix
changes; use `DOCS_BASE=/` for a website served at the server root.

The equivalent script command is:

```sh
python3 scripts/build-docs.py --base /manuals/QtProbe/
```

Validate an existing build using its matching prefix:

```sh
python3 scripts/build-docs.py --check-only --base /manuals/QtProbe/
```

Building does not publish the website. Node.js, npm, Python, Qt and EPICS are
not needed on the static web server.

## Edit the source document

| Content | Source |
| --- | --- |
| Operator behavior, command line, shortcuts, compatibility | `docs/qtprobe-guide.md` |
| Installation and platform build instructions | `README.md` |
| Test workflow and coverage | `qtprobe/tests/README.md` |
| Dated CPU measurements | `docs/performance.md` |
| Application screenshots | `docs/screenshots/` |
| License | `LICENSE` |
| Homepage, authors and this workflow | Authored pages under `docs/site/` |
| Navigation and shared presentation | `docs/site/.vitepress/` |

`scripts/sync-docs.py` imports every operator-guide section into a focused
page, preserving section anchors and rewriting links between pages. Generated
Markdown begins with a comment identifying its source; edit that source rather
than its imported copy. The same operator guide is embedded in QtProbe's
offline help. Rebuild the application after changing that guide.

Register new guide sections in `SECTIONS` in the sync script, add their routes
to navigation, and keep the exact generated paths in `docs/site/.gitignore`.
The build rejects missing or unregistered guide sections. Screenshots and the
original Markdown guide are copied into generated public directories.

## Check presentation

Check the homepage, an operator page and the command-line tables on desktop
and mobile, in light and dark appearance. Check search, sidebar navigation,
keyboard focus and cross-page anchors. Internal raw HTML links should use
page-relative paths; Vue components should use VitePress `withBase` when
constructing internal URLs. This keeps the website portable between `/` and
`/manuals/QtProbe/`.

## Cleanup

```sh
make docs-clean
make docs-distclean
```

`docs-clean` removes imported pages, copied assets, caches and generated HTML.
`docs-distclean` also removes documentation dependencies. Authored pages,
original guides and screenshots survive both commands. The application
`clean` and `distclean` targets include the corresponding documentation cleanup.
Run `make docs` to recreate the generated files.
