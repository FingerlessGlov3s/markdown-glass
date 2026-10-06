# Working on Markdown Glass

Notes for AI coding assistants (and a quick start for humans). Read
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) before changing the model, layout
or view: it lists the ownership rules and invariants that are not visible from
any single file.

## What this is

A read-only markdown viewer in C++20 and Qt 6 Widgets, rendering GitHub
Flavored Markdown natively (no web engine). Parser: vendored cmark-gfm in
`third_party/` (do not edit it). Syntax highlighting: KDE's
KSyntaxHighlighting.

## Commands

Everything runs in containers; the host only needs podman.

| Task | Command |
|---|---|
| Build | `scripts/build.sh` |
| All tests (offscreen) | `scripts/test.sh` |
| Tests under ASan + UBSan | `scripts/test.sh --sanitize` |
| Line coverage (fails below 85%) | `scripts/coverage.sh` |
| Fuzzing | `scripts/fuzz.sh [seconds-per-target]` |
| Format C++ | `scripts/format.sh` (`--check` to only check) |
| Static analysis | `scripts/lint.sh` (clang-tidy, `.clang-tidy`) |
| One distribution package | `scripts/package.sh <target>` (`list` for targets) |
| Every package | `scripts/package.sh all` |
| Distribution releases to follow | `scripts/distro-watch.py` (needs network) |

The full list of scripts, with what each produces, is in
[docs/BUILDING.md](docs/BUILDING.md#scripts).

## Rules

- **Mind the machine's memory.** Containers are capped (8 GB, 4 jobs; 6 GB
  each when `package.sh all` runs two at once). Never start several
  `package.sh` runs side by side by hand, and never raise the caps without
  asking. An uncapped parallel build once exhausted the owner's machine.
- **Never edit a shell script while it is running.** Bash reads scripts as it
  goes, so the running copy picks up the edit mid-way.
- **Keep the tests green and coverage at or above 85%** (`scripts/coverage.sh`).
  New behaviour comes with tests. Tests must run offscreen and must never
  open a real browser, editor or other program on the host.
- **Treat documents, images and socket messages as untrusted.** Keep the
  allow-lists in `model/htmlsubset.cpp`, `model/inlinebuilder.cpp`,
  `model/htmlblockreader.cpp`, `images/imageloader.cpp`, `images/publicaddress.cpp`,
  `images/svgcheck.cpp` and `app/links.cpp`; show
  their text in dialogs and tooltips only through `ui/plaintext.h`; add
  hostile cases to `tests/tst_hostile.cpp` when touching the parser, layout,
  images or links.
- **Pin what the build downloads.** Actions are pinned to commits and the
  AppImage's downloads to checksums; update the version and its checksum
  together.
- **Respect the layering and namespace rules** in docs/ARCHITECTURE.md.
- **Anything that keeps a pointer into a `Document` must be reset in
  `DocumentView::setDocument()`.**

## Style

- `.clang-format` defines the formatting; run `scripts/format.sh` before
  committing. `.editorconfig` covers everything else.
- The build has zero compiler warnings (flags in `MDGLASS_WARNINGS`, top-level
  `CMakeLists.txt`) and `scripts/lint.sh` reports nothing. Keep it that way:
  fix new findings rather than silencing them. CI checks formatting, lint,
  the sanitizer tests, coverage and a short fuzz run on every push to `main`,
  every pull request and every release tag, and
  treats warnings in our own code as errors (`MDGLASS_WERROR`).
- Comments explain *why*, not *what*. Keep them in step with the code.
- Name constants instead of writing bare numbers; layout spacing belongs in
  `Metrics` in `layout/layoutinternal.h`.
- File-local helpers go in an anonymous namespace.
- User-facing text uses British spelling ("colour") and a plain hyphen, never
  an em dash, except in window titles.
- Logging goes through the categories in `app/logging.h`.

## Releases

Each fact is written once and generated everywhere else:

- **Version:** `project()` in `CMakeLists.txt`.
- **Release notes:** `CHANGELOG.md`, one `## [version] - YYYY-MM-DD` section
  per release. `scripts/package/changelog.sh` turns it into the RPM
  `%changelog`, the Debian changelog, the AppStream release list and the
  GitHub release text.
- **Package summary and description:** `data/description.txt`.

The RPM spec (`packaging/markdown-glass.spec.in`) and the AppStream file
(`data/io.github.markdown_glass.metainfo.xml.in`) are templates; never put a
version or release notes in them. Pushing a `v<version>` tag first checks the
tag, `CMakeLists.txt` and `CHANGELOG.md` agree, then builds every package and
publishes a GitHub Release; see docs/BUILDING.md.
