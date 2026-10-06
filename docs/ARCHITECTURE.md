# Architecture

This is a map of the code for anyone changing it, human or AI. It explains how
a markdown file becomes pixels, who owns what, and the rules between modules
that are not visible from any one file. Read it before changing the model,
layout or view.

## The pipeline

```
file bytes
  -> md::parseMarkdown()            src/model/parser.cpp      (cmark-gfm AST -> Document)
  -> md::Document                   src/model/document.h      (blocks, inline text, outline)
  -> md::Layout(doc, Theme, LayoutOptions, CodeHighlighter, ImageSource)
                                    src/layout/               (positioned text boxes and shapes)
  -> Layout::paint() / hitTest()    src/layout/layoutpaint.cpp, layout.cpp
  -> DocumentView                   src/view/                 (scrolling, input, selection)
  -> DocumentPane / MainWindow      src/ui/                   (tabs, reload, history, menus)
```

The same `Layout` is used for the screen, the minimap (painted scaled down),
`--render-png` (`src/app/render.cpp`) and printing (`src/print/printing.cpp`,
with a light print theme and code always wrapped).

## Modules

| Directory | Responsibility | Depends on |
|---|---|---|
| `model/` | Parse markdown with cmark-gfm into `md::Document` (`parser.cpp`, with raw HTML blocks in `htmlblockreader.cpp` and the shared `Converter` in the private `converter.h`); tokenize the raw-HTML subset; tell text from binary files; open only regular files (`regularfile.cpp`). QtCore only, so it can be fuzzed. cmark-gfm's headers are visible to `model/` alone. | cmark-gfm |
| `layout/` | `md::Theme` (colours, fonts), `md::Layout` and its builder, painting, hit-testing, `md::ImageSource` interface. | model, highlight |
| `highlight/` | Syntax highlighting of fenced code via KSyntaxHighlighting, cached per block. | model |
| `print/` | Pagination and printing. | layout, highlight |
| `images/` | `ImageLoader`, the real `ImageSource`: decoding off the UI thread, remote fetching, the cache, animation; `svgcheck.cpp` refuses SVGs that reference anything outside themselves. | model, layout (interface only) |
| `view/` | `DocumentView` (the scrolling document widget) and `Minimap`. | layout, highlight |
| `ui/` | Windows, tabs, dialogs, outline sidebar, find bar. `DocumentPane` is split by job: `documentpane.cpp` (hosting the view, history, find), `documentpane_file.cpp` (reading, checks, live reload), `documentpane_links.cpp` (following links). `plaintext.h` shows untrusted text in dialogs and tooltips. | view, images, print, model, app |
| `app/` | Start-up, settings, single-instance handoff, link classification, launching the editor, `--render-png` and `--screenshot`, logging categories. | everything |

**Layering rule:** a module may include headers only from modules in earlier
tiers of this chain: model; then highlight, then layout (which uses
highlight); then print, images and view; then ui and app, which use each other
(the windows read settings, and start-up creates windows). A module never
includes from a later tier: nothing in `model/` or `layout/` may include from
`view/`, `ui/` or `app/`, for example. Two small headers are shared across the
layers on purpose: `app/logging.h` (logging categories, usable anywhere) and
`view/limits.h` (bounds and defaults that `app/settings.h` also uses).

**Namespace rule:** the toolkit-independent rendering core (`model/`,
`layout/`, `highlight/`, `print/`) is in namespace `md`. Widgets and
application classes (`images/`, `view/`, `ui/`, `app/`) are in the global
namespace, as is usual for Qt applications.

## Ownership and lifetimes

These are the rules most likely to be broken by a change.

1. **The document is owned by the view.** `DocumentView` holds a
   `std::unique_ptr<md::Document>` and a `std::unique_ptr<md::Layout>`.
2. **Everything else points into the document with raw pointers.** The
   layout's text boxes hold `const Block *`, `const InlineText *` and
   `const InlineImage *`. `CodeHighlighter` caches results keyed by
   `const Block *`. `OutlineDock` stores `const Block *` per heading.
   `DocumentView::m_toggled` (which `<details>` blocks the user opened)
   holds `const Block *`.
3. **So the order of replacement matters.** `DocumentView::setDocument()`
   destroys the old layout and clears the highlighter cache and toggles
   *before* the old document is released. `DocumentPane` emits
   `documentChanged()` after a new document is set, and `MainWindow` then
   refreshes the outline. Anything new that keeps a pointer into the document
   must be cleared in `setDocument()` too.
4. **Image results come back across threads.** `ImageLoader` decodes on
   `QThreadPool` workers and posts results back to the UI thread through
   `qApp` (which outlives the loader), guarded by a `QPointer` to the loader
   and a generation number. `clear()` bumps the generation so results for a
   previous document are dropped. See `ImageLoader::resultDelivery()`.

## Invariants

- **Text boxes are identified by index.** Selection, find matches, hover and
  copy targets refer to boxes by their index in `Layout::texts`. A re-layout
  produces the same box sequence for the same document and the same opened
  `<details>` blocks; when the number of boxes changes, `DocumentView`
  discards the selection.
- **Horizontal scroll positions live in the layout.** Each sideways-scrolling
  code block or table is a `ScrollFrame` whose `offset` the view sets
  directly. Offsets are carried across re-layouts by the frame's `Block *`
  (`DocumentView::doLayout()`).
- **`Hit::span` implies inline text.** `TextBox::spanAt()` only reports a
  span for boxes that have inline text, so `hit.span >= 0` means
  `box.inl != nullptr`. Use `Hit::inlineSpan()` and `Hit::href()` rather than
  indexing `box.inl` yourself.
- **Code lines are split once, one way.** Layout and highlighting both use
  `md::codeLines()`, so highlighted line N matches displayed line N.
- **Nesting is bounded twice.** The parser drops content nested deeper than
  `MaxDepth` (64) and the layout builder stops at 100, so recursion cannot be
  driven arbitrarily deep by a hostile file.
- **Tables are bounded in the model.** `Converter::table()` keeps at most
  `MaxTableColumns` columns and `MaxTableCells` cells, because every row is
  padded to the header's width and cmark-gfm accepts tens of thousands of
  columns. The layout may index `rows[r][c]` for any `c < columns.size()`.
- **`ImageSource::request()` starts loading.** It is not a plain getter:
  building a layout requests every image it contains. Loaders announce
  finished images (`ImageLoader::changed`) and the view re-lays out.

## Settings

`app/settings.h` holds the `Settings` struct and `AppSettings`, the single
shared instance. `forEachSetting()` in `settings.cpp` is the one list of
persisted keys used for both loading and saving. Bounds and defaults shared
with the view are in `view/limits.h`. Window geometry is saved separately by
`MainWindow`, as it is window state rather than a preference.

## Security boundaries

Markdown files, the images they reference, and messages on the
single-instance socket are untrusted. The rules are in the README's Security
section; where they live in code:

- Raw HTML is reduced to an allow-list: `model/htmlsubset.cpp`,
  `model/inlinebuilder.cpp`, `model/htmlblockreader.cpp`.
- Image formats, sizes and remote fetching: `images/imageloader.cpp`. SVGs
  must be self-contained and their `#id` references bounded in how far they
  fan out (`images/svgcheck.cpp`). Remote images only come from public
  addresses (`images/publicaddress.cpp`), checked again at every redirect;
  the connection goes to the address that was checked, with the host name
  kept for the Host header and TLS. At most `MaxConcurrentDownloads` are in
  flight; the rest wait in the loader's queue. `ImageLoader::setAddressCheck()`
  exists for tests only, which can run a server nowhere but on this machine.
- Which links may be acted on: `app/links.cpp` (which also strips `mailto:`
  links down to recipients, subject and body); the dialogs for the rest are
  in `DocumentPane::activateLink()` (`ui/documentpane_links.cpp`).
- Text from documents and file names reaches message boxes and tooltips only
  as plain text (`ui/plaintext.h`): Qt would otherwise render it as HTML,
  links and images included.
- The socket accepts only absolute paths, with a size limit:
  `App::handleMessage()` in `app/app.cpp`. It lives in the per-user runtime
  directory, so other local users cannot take its name; without such a
  directory there is no single-instance channel at all.

## Diagnostics

Logging categories are declared in `app/logging.h`. Debug output is off by
default; enable it with, for example:

```bash
QT_LOGGING_RULES="markdownglass.*.debug=true" markdown-glass README.md
```

## Tests

See [BUILDING.md](BUILDING.md#testing) for the suites and what each covers.
`tst_hostile` feeds deliberately nasty documents through the whole pipeline,
and the fuzz targets in `fuzz/` cover the parser, the HTML tokenizer, the
text check, the SVG check, the address check and link classification.
