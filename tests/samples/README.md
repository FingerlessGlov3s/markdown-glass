# Test samples

Files the test suites read (`SAMPLES_DIR` in `tests/CMakeLists.txt`):

| File | Used by |
|---|---|
| `basic.md` | `tst_layout` (pagination and printing of a document with every block type) |
| `gradient.png` | `tst_images` (a local image decodes off the UI thread; 120 x 60) |
| `anim.gif` | `tst_images` (animation: two 8 x 8 frames, 30 ms each, looping) |

The rest (`animated.md`, `images.md`, `badge.svg`, `bounce.gif`) are
documents for looking at the viewer by hand, each describing what it should
show. `scripts/fuzz.sh` also seeds its corpora from this directory: every
`*.md` file for the markdown and HTML targets, `*.svg` for the SVG check and
every file (this one included) for the text check. Check with
`grep -rn <name> tests/*.cpp` before removing or renaming a file.
