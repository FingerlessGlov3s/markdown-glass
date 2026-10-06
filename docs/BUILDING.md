# Building Markdown Glass

Everything builds and tests inside a Fedora 44 container, so the only thing
needed on the host is podman. The first run builds the container image, which
takes a few minutes.

## Quick start

```bash
git clone --recurse-submodules https://github.com/FingerlessGlov3s/markdown-glass
cd markdown-glass
scripts/build.sh rpm             # dist/fedora-44/markdown-glass-*.rpm
sudo dnf install ./dist/fedora-44/markdown-glass-*.x86_64.rpm
```

If you cloned without `--recurse-submodules`, fetch the parser with
`git submodule update --init`.

## Testing

```bash
scripts/test.sh                  # all test suites, offscreen
scripts/test.sh --sanitize       # the same under AddressSanitizer and UBSan
scripts/coverage.sh              # line coverage per file, plus an HTML report; fails below 85%
scripts/fuzz.sh 60               # fuzz the parser, HTML tokenizer, text, SVG, address and link checks
```

The tests also run inside every package build, so a failing test stops that
package from being produced.

| Suite | Covers |
|---|---|
| `tst_parser` | Markdown and the HTML subset into the document model |
| `tst_links` | Classifying links, including the ones that must never be launched |
| `tst_config` | Settings persistence, editor command parsing, finding the system editor |
| `tst_layout` | Wrapping, code blocks, tables, hit-testing, pagination, printing |
| `tst_images` | Allowed formats, hostile images, remote blocking, animation |
| `tst_hostile` | Deliberately nasty documents, images and links that must not crash, hang or be acted on |
| `tst_view` | The document view: copy buttons, selection, links, find, width limit |
| `tst_dialogs` | The settings and About dialogs |
| `tst_viewinput` | The view's keyboard, wheel and menus, auto-scroll, palette and resize |
| `tst_pane` | Loading, live reload, history, link dialogs, remote-image bar, find bar |
| `tst_navigation` | The outline sidebar and the minimap |
| `tst_mainwindow` | Tabs, menu and toolbar actions, dialogs, printing, drag and drop |
| `tst_instance` | The single-instance handoff, new windows versus tabs |
| `tst_cli` | `--version`, `--help`, `--render-png` and `--screenshot` |

**Coverage aim: at least 85% of lines.** Run `scripts/coverage.sh` to check;
it prints a per-file table, writes `build/coverage/coverage.html`, and fails
below 85% (CI runs it on pushes to `main`, pull requests and release tags). New code
should come with tests that keep the total at or above the aim. The aim is
deliberately not 100%: some error paths (a disk failing mid-read, for example)
cost more to simulate than they are worth.

## Packages for every distribution

`scripts/package.sh` builds, tests and packages one target inside that
distribution's own container, the same way the GitHub Actions workflow does.

```bash
scripts/package.sh list          # show the targets
scripts/package.sh debian-13     # dist/debian-13/*.deb
scripts/package.sh appimage      # dist/appimage/*.AppImage
scripts/package.sh all           # every target, two at a time
```

| Target | Container | Package |
|---|---|---|
| `fedora-43`, `fedora-44` | Fedora | RPM |
| `almalinux-10` | AlmaLinux 10 with EPEL | RPM |
| `opensuse-tumbleweed`, `opensuse-leap-16` | openSUSE | RPM |
| `debian-13`, `ubuntu-26.04`, `ubuntu-26.10` | Debian / Ubuntu | .deb (CPack) |
| `appimage` | Ubuntu 22.04 | AppImage |

Each container is limited to 8 GB of memory and 4 parallel compile jobs, and a
build will not start unless 10 GB is free. `all` builds two targets at a time
with 6 GB each, writes one log per target to `build/logs/`, and waits for
memory to free up before starting the next. Change the limits with
`MEMORY_GB`, `JOBS` and `PARALLEL`, for example
`JOBS=2 MEMORY_GB=4 scripts/package.sh debian-13` or
`PARALLEL=1 scripts/package.sh all`. Don't start several `package.sh` runs by
hand at once; they don't know about each other's memory use.

Packages are built for the CPU of the machine running the script. CI builds
every target on both x86_64 and arm64 runners.

The build containers get only what they need. The checkout is mounted
read-only, with just `dist/` (and the AppImage cache) writable, and the git
directory hidden behind an empty tmpfs (so `.git` must be a directory, as in
a plain clone; a worktree is not supported). They keep the network, because
the package managers need it, but every capability is dropped except the
handful that apt, dnf, zypper and rpm use to install packages, no process
may gain privileges, and the number of processes is limited. Every
download the AppImage build makes (Qt, KSyntaxHighlighting, CMake and the
AppImage tools) is pinned to a version and checked against a SHA-256 or a
git commit; to move to a new version, update the version and its checksum
together at the top of `scripts/package/build-appimage.sh`. The development
container (`scripts/container.sh`) also runs without network or
capabilities. Under CI (`CI` set) the free-memory refusal is skipped, as the
runner is a throwaway machine.

What the RPM build checks: the test suite (`%check`), `desktop-file-validate`,
and on Fedora `appstreamcli validate --pedantic` on the generated AppStream
file and `annocheck` on the binary are all enforced and fail the build.
`rpmlint` is reported, not enforced: its output is in the log, with the
known false positives (British spelling in the description) filtered by
`packaging/markdown-glass.rpmlintrc`, and the rest, such as openSUSE's
branding and dependency policy, left visible.

Every package installs `LICENSE`, `LICENSE.cmark-gfm` (the parser's `COPYING`)
and `README.md` in `/usr/share/doc/markdown-glass/` (openSUSE:
`/usr/share/doc/packages/markdown-glass/`). The `.deb` adds Debian's
`copyright` file (`packaging/copyright`, DEP-5) and `changelog.Debian.gz`;
the AppImage adds `THIRD-PARTY-NOTICES` naming the versions and sources of
the Qt and KSyntaxHighlighting it bundles.

The AppImage build compiles a minimal Qt from source the first time (no ICU,
no OpenGL, only the modules the viewer uses), which takes a while. It is
cached in `.cache/appimage/` and reused afterwards; the cache key includes
the hardening flags, which the bundled Qt and KSyntaxHighlighting are built
with as well as the viewer, so changing them rebuilds Qt. It is built on
Ubuntu 22.04 on purpose: an AppImage bundles its libraries but not glibc, so
the oldest base gives the widest compatibility.

Ubuntu 24.04 has no package because it ships Qt 6.4 and no KDE Frameworks 6;
the AppImage covers it.

## Releases

`.github/workflows/build.yml` runs on every push to `main`, every pull
request and every release tag. It checks formatting, runs clang-tidy and
shellcheck, the sanitizer tests, coverage (failing below 85%) and a short
fuzz run, treats compiler warnings in our own code as errors, and builds
every target, keeping the packages as workflow artifacts. Its actions are
pinned to commits; Dependabot (`.github/dependabot.yml`) proposes updates
once a month to the actions, to the development container's base image and
to the cmark-gfm submodule. A submodule pull request is a reminder rather
than something to merge as is: the parser's version is also written in the
RPM spec and in this file, and a new parser deserves a run of the hostile
tests.

Once a month `.github/workflows/distro-watch.yml` runs
`scripts/distro-watch.py`, which compares the package targets with
endoflife.date and keeps a single issue labelled `distro-watch` listing new
releases to add and targets past their end of life. Run the script by hand
to see the same report.

To publish a release:

1. Set the new version in `project()` in `CMakeLists.txt`. This is the only
   place the version is written.
2. In `CHANGELOG.md`, rename `## [Unreleased]` to `## [0.2.0] - YYYY-MM-DD`
   (today's date) and start a new empty `## [Unreleased]` above it.
3. Commit, then tag and push:

```bash
git tag v0.2.0
git push origin v0.2.0
```

The first job checks that the tag matches `CMakeLists.txt` and that
`CHANGELOG.md` has a dated section for it; if not, nothing else runs: the
checks and the package builds all wait for it. Versions are digits and dots
only (`project(VERSION)` in CMake and the tag check accept nothing else), so
pre-release strings such as `1.0.0-rc1` are not supported by the pipeline.
Then every package is built and tested, and a GitHub Release is published with
every package, a `SHA256SUMS` file, and that version's `CHANGELOG.md`
section as its notes. The packages carry build provenance attestations, so
anyone can check a download came from this workflow:

```bash
gh attestation verify Markdown_Glass-0.2.0-x86_64.AppImage --repo FingerlessGlov3s/markdown-glass
```

(Attestations need the repository to be public, or a paid plan.)

The same section becomes the RPM changelog (`rpm -q --changelog`), the
Debian changelog (`/usr/share/doc/markdown-glass/changelog.Debian.gz`) and
the release notes software centres show, via `scripts/package/changelog.sh`.
Preview them with, for example, `scripts/package/changelog.sh rpm`.

## Scripts

The full list; [AGENTS.md](../AGENTS.md) has the short one.

| Command | What it does |
|---|---|
| `scripts/build.sh` | Builds `build/default/src/markdown-glass` |
| `scripts/build.sh rpm` | Shortcut for `scripts/package.sh fedora-44`: the Fedora 44 RPM in `dist/fedora-44/`, with tests, an `rpmlint` report, AppStream validation and a hardening check |
| `scripts/package.sh <target>` | Builds, tests and packages one distribution target (see above) |
| `scripts/test.sh` | Unit and interaction tests, run offscreen |
| `scripts/test.sh --sanitize` | The same tests under AddressSanitizer and UBSan |
| `scripts/coverage.sh` | Line coverage of the tests; fails below 85% |
| `scripts/format.sh` | Formats the C++ with clang-format (`--check` to only check) |
| `scripts/lint.sh` | Static analysis: clang-tidy over the C++ and shellcheck over the scripts; fails on any finding, or if a file cannot be analysed |
| `scripts/fuzz.sh 60` | libFuzzer on the parser, HTML tokenizer, text check, SVG check, address check and link classifier, seconds per target |
| `scripts/distro-watch.py` | Reports distribution releases to add as targets and targets past end of life (needs network) |
| `scripts/screenshot.sh` | Regenerates `docs/images/screenshot.png` from `docs/showcase.md` |
| `scripts/container.sh <command>` | Runs any command inside the build container |

## Checking rendering without a display

```bash
scripts/container.sh build/default/src/markdown-glass --render-png out.png README.md
scripts/container.sh build/default/src/markdown-glass --screenshot window.png README.md
```

`--render-png` draws just the document (add `--dark`, `--width N` or
`--no-code-wrap`); `--screenshot` captures the whole window.

## Building without the container

On Fedora 44 the build needs `gcc-c++`, `cmake`, `ninja-build`,
`qt6-qtbase-devel`, `qt6-qtsvg-devel` and `kf6-syntax-highlighting-devel`,
plus `qt6-qtimageformats` at run time for WebP images
(see [scripts/Containerfile](../scripts/Containerfile) for the full list), then:

```bash
cmake -S . -B build/default -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/default
ctest --test-dir build/default
```

## Dependencies

- Qt 6: Widgets, PrintSupport and Network, plus the SVG and image-format
  plugins at run time.
- KDE Frameworks 6 SyntaxHighlighting.
- [cmark-gfm](https://github.com/github/cmark-gfm), GitHub's markdown parser,
  vendored as a git submodule in `third_party/`, pinned to 0.29.0.gfm.13 and
  linked statically.

## Code structure

See [ARCHITECTURE.md](ARCHITECTURE.md) for how the pieces fit together, who
owns what, and the rules between modules. [AGENTS.md](../AGENTS.md) has the
working rules for anyone, human or AI, changing the code.

## Local output

Everything generated stays out of the source tree:

| Path | Contents |
|---|---|
| `build/default`, `build/sanitize`, `build/coverage`, `build/fuzz`, `build/lint` | Builds from `build.sh`/`test.sh`, `test.sh --sanitize`, `coverage.sh`, `fuzz.sh` and `lint.sh` (with clang-tidy's report) |
| `build/logs` | One log per target from `package.sh all` |
| `dist/<target>` | Finished packages |
| `.cache/appimage` | The AppImage's self-built Qt, kept between builds because it is slow to make |

All of these are ignored by git and safe to delete.

## Source layout

The modules under `src/` and what each may depend on are listed in
[ARCHITECTURE.md](ARCHITECTURE.md#modules). The rest of the tree:

| Path | Contents |
|---|---|
| `tests` | Test suites and sample documents (`tests/samples/README.md` says which are used by tests) |
| `scripts` | Build, test and packaging scripts, and the development `Containerfile` |
| `scripts/package` | The per-distribution packaging steps that `package.sh` runs inside each container |
| `fuzz` | Fuzz targets |
| `data` | Desktop file, icon, AppStream metadata template and the package description |
| `packaging` | The RPM spec template and its rpmlint filters, and the Debian copyright file |
