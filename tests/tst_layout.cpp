#include "highlight/highlighter.h"
#include "layout/layout.h"
#include "model/parser.h"
#include "print/printing.h"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QPrinter>
#include <QTemporaryDir>
#include <QTest>

using namespace md;

namespace {

// Images named "ready-..." have loaded, at 120 by 80; the rest are loading.
// Remembers what was asked for, which is what starts loading.
class FakeImages : public ImageSource
{
public:
    State request(const QString &src) override
    {
        requested.insert(src);
        return src.startsWith(QStringLiteral("ready")) ? Ready : Pending;
    }
    QSizeF naturalSize(const QString &) override { return QSizeF(120, 80); }
    void paint(QPainter &, const QString &, const QRectF &, bool) override { }

    QSet<QString> requested;
};

} // namespace

class TestLayout : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { m_theme = Theme::fromPalette(QGuiApplication::palette(), 1.0); }
    // The highlighter caches by block address: one test's freed blocks must
    // not answer for the next test's, as the view clears it per document.
    void cleanup() { m_highlighter.clear(); }
    void proseAlwaysWraps();
    void codeWrapSetting();
    void highlightingOnlyWithLanguage();
    void wideTableScrolls();
    void detailsToggle();
    void hitTestAndCopy();
    void pagination();
    void paginationWithoutRoom();
    void copyTextUsesAltText();
    void sampleRenders();
    void printsToPdf();
    void reuseMatchesFreshLayout();
    void reuseAcrossDetailsToggle();
    void parallelMatchesSerial();
    void highlighterCache();

private:
    std::unique_ptr<Layout> build(const Document &doc, qreal width, bool wrapCode = true,
                                  std::unique_ptr<Layout> previous = nullptr, int maxThreads = 0,
                                  const QSet<const Block *> *toggled = nullptr, ImageSource *images = nullptr)
    {
        LayoutOptions options;
        options.width = width;
        options.wrapCode = wrapCode;
        options.maxThreads = maxThreads;
        options.toggledDetails = toggled;
        return std::make_unique<Layout>(doc, m_theme, options, &m_highlighter, images, std::move(previous));
    }

    Theme m_theme;
    CodeHighlighter m_highlighter;
};

static const QByteArray LongLine = QByteArray("word ").repeated(200);

void TestLayout::proseAlwaysWraps()
{
    const auto doc = parseMarkdown(LongLine + "\n");
    for (bool wrapCode : {true, false}) {
        const auto layout = build(*doc, 400, wrapCode);
        QCOMPARE(layout->texts.size(), size_t(1));
        const TextBox &box = *layout->texts[0];
        QVERIFY(box.layout.lineCount() > 5);
        QVERIFY(box.naturalWidth <= 400.5);
        QVERIFY(layout->frames.empty());
    }
}

void TestLayout::codeWrapSetting()
{
    const auto doc = parseMarkdown("```\n" + LongLine + "\nshort\n```\n");

    const auto wrapped = build(*doc, 400, true);
    QVERIFY(wrapped->frames.empty());
    QVERIFY(wrapped->texts[0]->layout.lineCount() > 3);

    const auto scrolling = build(*doc, 400, false);
    QCOMPARE(scrolling->frames.size(), size_t(1));
    QCOMPARE(scrolling->texts[0]->layout.lineCount(), 1);
    QVERIFY(scrolling->frames[0].maxOffset() > 400);
    QVERIFY(scrolling->height() < wrapped->height());

    // Code that fits needs no scrollbar even with wrapping off.
    const auto fits = parseMarkdown("```\nshort\n```\n");
    QVERIFY(build(*fits, 400, false)->frames.empty());
}

void TestLayout::highlightingOnlyWithLanguage()
{
    const auto doc =
        parseMarkdown("```\necho \"hi\" # c\n```\n\n```bash\necho \"hi\" # c\n```\n\n```nosuchlanguage\nx\n```\n");
    QVERIFY(m_highlighter.highlight(*doc->blocks[0]) == nullptr);
    const HighlightedLines *bash = m_highlighter.highlight(*doc->blocks[1]);
    QVERIFY(bash != nullptr);
    QCOMPARE(bash->size(), 1);
    QVERIFY(!bash->first().isEmpty());
    QVERIFY(m_highlighter.highlight(*doc->blocks[2]) == nullptr);

    // Common fence names and aliases map to a syntax.
    const auto aliases = parseMarkdown("```sh\nx=1\n```\n\n```python\nx = 1\n```\n\n```cpp\nint x;\n```\n");
    for (const auto &block : aliases->blocks)
        QVERIFY2(m_highlighter.highlight(*block) != nullptr, qPrintable(block->language));
}

void TestLayout::wideTableScrolls()
{
    const QByteArray word(80, 'x');
    const auto doc = parseMarkdown("| a | b |\n|---|---|\n| " + word + " | " + word + " |\n");
    const auto narrow = build(*doc, 300);
    QCOMPARE(narrow->frames.size(), size_t(1));
    QVERIFY(narrow->frames[0].maxOffset() > 0);
    QVERIFY(build(*doc, 4000)->frames.empty());
}

void TestLayout::detailsToggle()
{
    const auto doc = parseMarkdown("<details>\n<summary>Sum</summary>\n\nHidden text.\n\n</details>\n");
    const auto closed = build(*doc, 600);
    QCOMPARE(closed->texts.size(), size_t(1));
    QCOMPARE(closed->toggles.size(), size_t(1));

    QSet<const Block *> toggled {closed->toggles[0].block};
    LayoutOptions options;
    options.width = 600;
    options.toggledDetails = &toggled;
    const Layout open(*doc, m_theme, options, &m_highlighter, nullptr);
    QCOMPARE(open.texts.size(), size_t(2));
    QVERIFY(open.height() > closed->height());
}

void TestLayout::hitTestAndCopy()
{
    const auto doc = parseMarkdown("First paragraph with `some code` inside.\n\nSecond [link](other.md) here.\n");
    const auto layout = build(*doc, 600);
    QCOMPARE(layout->texts.size(), size_t(2));

    const TextBox &first = *layout->texts[0];
    const int codeStart = first.layout.text().indexOf(QStringLiteral("some"));
    const auto rects = first.rangeRects(codeStart, 9, LineBox::Glyphs);
    QCOMPARE(rects.size(), 1);
    const Hit onCode = layout->hitTest(layout->toDocument(first, rects[0]).center());
    QVERIFY(onCode.inside);
    QCOMPARE(onCode.box, 0);
    QVERIFY(first.inl->spans[onCode.span].flags & Span::Code);
    QCOMPARE(first.copyText(first.inl->spans[onCode.span].start, first.inl->spans[onCode.span].length),
             QStringLiteral("some code"));

    const TextBox &second = *layout->texts[1];
    const int linkStart = second.layout.text().indexOf(QStringLiteral("link"));
    const Hit onLink =
        layout->hitTest(layout->toDocument(second, second.rangeRects(linkStart, 4, LineBox::Glyphs)[0]).center());
    QCOMPARE(onLink.box, 1);
    QCOMPARE(second.inl->links.value(onLink.link(second)), QStringLiteral("other.md"));

    // Far below the document: nearest box, but not inside it.
    const Hit below = layout->hitTest(QPointF(10, layout->height() + 500));
    QCOMPARE(below.box, 1);
    QVERIFY(!below.inside);
}

void TestLayout::pagination()
{
    QByteArray source;
    for (int i = 0; i < 120; ++i)
        source += "## Section " + QByteArray::number(i)
            + "\n\nSome paragraph text that is long enough to wrap a little bit on a page. "
              "It goes on for a second sentence.\n\n```\nline one\nline two\nline three\n```\n\n";
    const auto doc = parseMarkdown(source);
    const auto layout = build(*doc, 640);
    const qreal pageHeight = 900;
    const QList<qreal> breaks = paginate(*layout, pageHeight);

    QVERIFY(breaks.size() > 5);
    QCOMPARE(breaks.last(), layout->height());
    qreal previous = 0;
    for (qreal y : breaks) {
        QVERIFY(y > previous);
        QVERIFY(y - previous <= pageHeight + 0.01);
        previous = y;
    }
    // No break cuts through a line of text.
    for (const auto &box : layout->texts) {
        for (const auto &extent : box->lineExtents()) {
            const qreal top = box->pos.y() + extent.first;
            const qreal bottom = box->pos.y() + extent.second;
            for (qreal y : breaks)
                QVERIFY2(!(y > top + 0.01 && y < bottom - 0.01), "page break inside a text line");
        }
    }
}

void TestLayout::paginationWithoutRoom()
{
    // A page height of nothing (or less) could never advance: one page, the
    // whole document, rather than an endless list of breaks.
    const auto doc = parseMarkdown("# Top\n\nOne paragraph.\n\nAnother paragraph.\n");
    const auto layout = build(*doc, 640);
    QVERIFY(layout->height() > 0);
    QCOMPARE(paginate(*layout, 0), QList<qreal> {layout->height()});
    QCOMPARE(paginate(*layout, -5), QList<qreal> {layout->height()});
}

void TestLayout::copyTextUsesAltText()
{
    const auto doc = parseMarkdown("before ![the alt text](x.png) after\n");
    const auto layout = build(*doc, 600);
    QCOMPARE(layout->texts.size(), size_t(1));
    const TextBox &box = *layout->texts[0];
    QCOMPARE(box.inl->images.size(), 1);
    // The placeholder glyph is replaced by the alt text when copying.
    QCOMPARE(box.copyText(0, int(box.layout.text().size())), QStringLiteral("before the alt text after"));
    // Only when the image lies within the copied range.
    QCOMPARE(box.copyText(0, 6), QStringLiteral("before"));
}

void TestLayout::sampleRenders()
{
    QFile file(QStringLiteral(SAMPLES_DIR "/basic.md"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto doc = parseMarkdown(file.readAll());
    const auto layout = build(*doc, 800);
    QVERIFY(layout->height() > 500);
    QVERIFY(layout->codeAreas.size() == 3);

    QImage image(800, int(layout->height()), QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter p(&image);
    PaintState state;
    state.selStartBox = 0;
    state.selEndBox = int(layout->texts.size()) - 1;
    state.selEndPos = 3;
    layout->paint(p, QRectF(0, 0, 800, layout->height()), state);
    p.end();
    // Something other than background was drawn.
    bool drawn = false;
    for (int y = 0; y < image.height() && !drawn; y += 7) {
        for (int x = 0; x < image.width(); x += 5)
            drawn |= image.pixel(x, y) != QColor(Qt::white).rgb();
    }
    QVERIFY(drawn);
}

void TestLayout::printsToPdf()
{
    QFile file(QStringLiteral(SAMPLES_DIR "/basic.md"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto doc = parseMarkdown(file.readAll() + QByteArray("\n\nMore text.\n").repeated(150));

    QTemporaryDir dir;
    const QString pdf = dir.filePath(QStringLiteral("out.pdf"));
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(pdf);
    printDocument(&printer, *doc, nullptr);

    QFile out(pdf);
    QVERIFY(out.open(QIODevice::ReadOnly));
    const QByteArray data = out.readAll();
    QVERIFY(data.startsWith("%PDF"));
    QVERIFY(data.count("/Type /Page\n") + data.count("/Type /Page ") >= 2 || data.size() > 20000);
}

namespace {

// Parts of a layout built on several threads are stacked afterwards, so their
// positions may differ from a one-thread layout by rounding, nothing more.
constexpr qreal Rounding = 1e-3;

bool near(qreal a, qreal b)
{
    return std::abs(a - b) <= Rounding;
}

bool near(const QRectF &a, const QRectF &b)
{
    return near(a.x(), b.x()) && near(a.y(), b.y()) && near(a.width(), b.width()) && near(a.height(), b.height());
}

void collectBlocks(const std::vector<std::unique_ptr<Block>> &list, QList<const Block *> &out)
{
    for (const auto &b : list) {
        out.append(b.get());
        collectBlocks(b->children, out);
    }
}

// A format range as text, for saying how two of them differ.
QString describe(const QTextLayout::FormatRange &range)
{
    QStringList properties;
    const QMap<int, QVariant> all = range.format.properties();
    for (auto it = all.cbegin(); it != all.cend(); ++it) {
        const QVariant &v = it.value();
        QString value = v.toString();
        if (v.metaType() == QMetaType::fromType<QBrush>())
            value = v.value<QBrush>().color().name(QColor::HexArgb);
        else if (v.metaType() == QMetaType::fromType<QFont>())
            value = v.value<QFont>().toString();
        else if (value.isEmpty())
            value = QString::fromLatin1(v.typeName());
        properties << QStringLiteral("0x%1=%2").arg(it.key(), 0, 16).arg(value);
    }
    return QStringLiteral("[%1+%2 %3]").arg(range.start).arg(range.length).arg(properties.join(u' '));
}

// The first difference between two layouts of the same document, or an empty
// string when they place the same text in the same lines at the same spots.
QString difference(const Layout &a, const Layout &b, const Document &doc)
{
    if (a.texts.size() != b.texts.size())
        return QStringLiteral("box count %1 / %2").arg(a.texts.size()).arg(b.texts.size());
    if (!near(a.height(), b.height()))
        return QStringLiteral("height %1 / %2").arg(a.height()).arg(b.height());
    for (size_t i = 0; i < a.texts.size(); ++i) {
        const TextBox &x = *a.texts[i];
        const TextBox &y = *b.texts[i];
        const QString box = QStringLiteral("box %1 (%2): ").arg(i).arg(x.layout.text().left(40));
        if (!near(x.rect(), y.rect()) || x.naturalWidth != y.naturalWidth)
            return box + QStringLiteral("geometry");
        if (x.frame != y.frame || x.separator != y.separator || x.block != y.block || x.inl != y.inl)
            return box + QStringLiteral("attributes");
        // Fonts, colours and highlighting.
        if (x.layout.font() != y.layout.font())
            return box + QStringLiteral("font %1 / %2").arg(x.layout.font().toString(), y.layout.font().toString());
        const QList<QTextLayout::FormatRange> fx = x.layout.formats();
        const QList<QTextLayout::FormatRange> fy = y.layout.formats();
        if (fx != fy) {
            QString what = box + QStringLiteral("formats, %1 / %2 ranges").arg(fx.size()).arg(fy.size());
            for (qsizetype r = 0; r < qMin(fx.size(), fy.size()); ++r) {
                if (fx[r] != fy[r]) {
                    what += QStringLiteral(", range %1: %2 / %3").arg(r).arg(describe(fx[r]), describe(fy[r]));
                    break;
                }
            }
            return what;
        }
        if (x.layout.lineCount() != y.layout.lineCount())
            return box + QStringLiteral("line count %1 / %2").arg(x.layout.lineCount()).arg(y.layout.lineCount());
        for (int l = 0; l < x.layout.lineCount(); ++l) {
            const QTextLine p = x.layout.lineAt(l);
            const QTextLine q = y.layout.lineAt(l);
            // cursorToX() of the line's start includes the offset alignment gives it.
            if (p.textStart() != q.textStart() || p.textLength() != q.textLength() || p.y() != q.y()
                || p.cursorToX(p.textStart()) != q.cursorToX(q.textStart()))
                return box + QStringLiteral("line %1").arg(l);
        }
        for (qsizetype s = 0; s < x.images.size(); ++s) {
            if (x.images[s].rect != y.images[s].rect)
                return box + QStringLiteral("image %1").arg(s);
        }
    }
    if (a.frames.size() != b.frames.size())
        return QStringLiteral("frame count %1 / %2").arg(a.frames.size()).arg(b.frames.size());
    for (size_t i = 0; i < a.frames.size(); ++i) {
        if (!near(a.frames[i].rect, b.frames[i].rect) || a.frames[i].contentWidth != b.frames[i].contentWidth
            || a.frames[i].block != b.frames[i].block)
            return QStringLiteral("frame %1").arg(i);
    }
    if (a.decos.size() != b.decos.size())
        return QStringLiteral("deco count %1 / %2").arg(a.decos.size()).arg(b.decos.size());
    for (size_t i = 0; i < a.decos.size(); ++i) {
        if (!near(a.decos[i].rect, b.decos[i].rect) || a.decos[i].frame != b.decos[i].frame
            || a.decos[i].kind != b.decos[i].kind)
            return QStringLiteral("deco %1").arg(i);
    }
    if (a.codeAreas.size() != b.codeAreas.size() || a.toggles.size() != b.toggles.size()
        || a.unbreakable.size() != b.unbreakable.size())
        return QStringLiteral("code areas, toggles or unbreakable extents");
    for (size_t i = 0; i < a.toggles.size(); ++i) {
        if (!near(a.toggles[i].rect, b.toggles[i].rect) || a.toggles[i].block != b.toggles[i].block)
            return QStringLiteral("toggle %1").arg(i);
    }
    for (qsizetype i = 0; i < a.unbreakable.size(); ++i) {
        if (!near(a.unbreakable[i].first, b.unbreakable[i].first)
            || !near(a.unbreakable[i].second, b.unbreakable[i].second))
            return QStringLiteral("unbreakable extent %1").arg(i);
    }
    QList<const Block *> blocks;
    collectBlocks(doc.blocks, blocks);
    for (const Block *block : std::as_const(blocks)) {
        if (!near(a.blockTop(block), b.blockTop(block)))
            return QStringLiteral("top of the block on line %1").arg(block->sourceLine);
    }
    return QString();
}

// Everything that decides whether a text box can keep its lines at a new
// width: wrapping, trailing spaces, alignment, right-to-left text, images,
// tables measured once, scrolling frames, nesting and footnotes.
QByteArray mixedSection(int n)
{
    const QByteArray id = QByteArray::number(n);
    QByteArray s = "## Section " + id + "\n\n" + LongLine.left(700) + "\n\n";
    s += "```\ntrailing spaces    \nshort line\n" + LongLine.left(400) + "\n\tindented\tby tabs\n\n```\n\n";
    s += "```bash\necho \"" + id + "\" | grep x   # trailing comment \n```\n\n";
    s += "| centred | right | plain |\n|:-:|--:|---|\n";
    s += "| a | b | " + LongLine.left(160) + "|\n| **c** | `d` | e |\n";
    s += "| ![t](ready-cell-" + id + ".png) | ![u](cell-" + id + ".png) | f |\n\n";
    s += "|" + QByteArray("column|").repeated(40) + "\n|" + QByteArray("-|").repeated(40) + "\n|"
        + QByteArray("value|").repeated(40) + "\n\n";
    s += "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d \xd7\xa2\xd7\x95\xd7\x9c\xd7\x9d short right-to-left\n\n";
    s += "before ![alt " + id + "](x.png) after\n\n";
    s += "A loaded ![r](ready-" + id + ".png) image " + LongLine.left(120) + "\n\n";
    s += "<p align=\"center\">centred paragraph</p>\n\n";
    s += "<details><summary>More " + id + "</summary>\n\nHidden " + LongLine.left(200) + " ![h](ready-hidden-" + id
        + ".png)\n\n</details>\n\n";
    s += "<details open><summary>Open " + id + "</summary>\n\n```\ninside\n```\n\n</details>\n\n";
    s += "> quoted " + LongLine.left(300) + "\n\n";
    s += "- item\n- [x] task\n  1. nested " + LongLine.left(150) + "\n\n";
    s += "Note[^" + id + "].\n\n[^" + id + "]: The footnote " + id + ".\n\n";
    return s;
}

} // namespace

void TestLayout::reuseMatchesFreshLayout()
{
    const auto doc = parseMarkdown(mixedSection(1) + mixedSection(2));
    FakeImages images;
    for (bool wrap : {true, false}) {
        auto reused = build(*doc, 600, wrap, nullptr, 1, nullptr, &images);
        // Narrower, much narrower, wider, the same again, and back.
        for (qreal width : {450.0, 180.0, 900.0, 900.0, 600.0}) {
            reused = build(*doc, width, wrap, std::move(reused), 1, nullptr, &images);
            const auto fresh = build(*doc, width, wrap, nullptr, 1, nullptr, &images);
            const QString diff = difference(*reused, *fresh, *doc);
            QVERIFY2(diff.isEmpty(),
                     qPrintable(QStringLiteral("wrap %1, width %2: %3").arg(wrap).arg(width).arg(diff)));
        }
    }
}

void TestLayout::reuseAcrossDetailsToggle()
{
    const auto doc = parseMarkdown(mixedSection(1) + mixedSection(2) + mixedSection(3));
    QList<const Block *> closed;
    for (const auto &b : doc->blocks) {
        if (b->type == Block::Details && !b->open)
            closed.append(b.get());
    }
    QCOMPARE(closed.size(), 3);

    // Opening a block in the middle, then closing it again: boxes before it
    // are reused, the rest are built afresh.
    QSet<const Block *> toggled;
    auto layout = build(*doc, 600, true, nullptr, 1, &toggled);
    const size_t closedBoxes = layout->texts.size();
    toggled.insert(closed[1]);
    layout = build(*doc, 600, true, std::move(layout), 1, &toggled);
    QVERIFY(layout->texts.size() > closedBoxes);
    QString diff = difference(*layout, *build(*doc, 600, true, nullptr, 1, &toggled), *doc);
    QVERIFY2(diff.isEmpty(), qPrintable(diff));

    toggled.clear();
    layout = build(*doc, 500, true, std::move(layout), 1, &toggled);
    QCOMPARE(layout->texts.size(), closedBoxes);
    diff = difference(*layout, *build(*doc, 500, true, nullptr, 1, &toggled), *doc);
    QVERIFY2(diff.isEmpty(), qPrintable(diff));
}

void TestLayout::parallelMatchesSerial()
{
    // Long enough (in source lines) to be built in parts.
    QByteArray source;
    for (int i = 1; i <= 60; ++i)
        source += mixedSection(i);
    const auto doc = parseMarkdown(source);
    QVERIFY(doc->blocks.back()->sourceLine > 2000);

    for (bool wrap : {true, false}) {
        // Images are asked about on this thread for the parts built on others,
        // exactly the ones a one-thread layout asks about: not those in
        // closed details blocks.
        FakeImages parallelImages;
        FakeImages serialImages;
        auto parallel = build(*doc, 700, wrap, nullptr, 4, nullptr, &parallelImages);
        QString diff = difference(*parallel, *build(*doc, 700, wrap, nullptr, 1, nullptr, &serialImages), *doc);
        QVERIFY2(diff.isEmpty(), qPrintable(diff));
        QCOMPARE(parallelImages.requested, serialImages.requested);
        QVERIFY(serialImages.requested.contains(QStringLiteral("ready-cell-7.png")));
        QVERIFY(!serialImages.requested.contains(QStringLiteral("ready-hidden-7.png")));

        // A part's boxes are reused by the same part after a resize.
        parallel = build(*doc, 350, wrap, std::move(parallel), 4, nullptr, &parallelImages);
        const auto serial = build(*doc, 350, wrap, nullptr, 1, nullptr, &serialImages);
        diff = difference(*parallel, *serial, *doc);
        QVERIFY2(diff.isEmpty(), qPrintable(diff));

        for (qreal y = 0; y < serial->height(); y += serial->height() / 37) {
            const Hit p = parallel->hitTest(QPointF(100, y));
            const Hit s = serial->hitTest(QPointF(100, y));
            QCOMPARE(p.box, s.box);
            QCOMPARE(p.pos, s.pos);
            QCOMPARE(parallel->blockAt(y), serial->blockAt(y));
        }

        // Boxes laid out on threads that have finished still paint here.
        QImage image(400, 600, QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        parallel->paint(painter, QRectF(0, parallel->height() / 2, 400, 600), PaintState());
    }

    // Just two top-level blocks, the first of them thousands of lines long:
    // two parts, one block each.
    const auto twoBlocks = parseMarkdown("```\n" + QByteArray("a line of code\n").repeated(3000) + "```\n\nThe end.\n");
    QCOMPARE(twoBlocks->blocks.size(), size_t(2));
    const QString diff =
        difference(*build(*twoBlocks, 500, true, nullptr, 4), *build(*twoBlocks, 500, true, nullptr, 1), *twoBlocks);
    QVERIFY2(diff.isEmpty(), qPrintable(diff));
}

void TestLayout::highlighterCache()
{
    const auto doc = parseMarkdown("```cpp\nint x;\n```\n\n```nosuchlanguage\nx\n```\n\n```cpp\nint y;\n```\n");
    CodeHighlighter highlighter;
    const Block &cpp = *doc->blocks[0];
    const Block &unknown = *doc->blocks[1];
    QVERIFY(!highlighter.cached(cpp)); // not highlighted yet
    const HighlightedLines *lines = highlighter.highlight(cpp);
    QVERIFY(lines);
    QCOMPARE(highlighter.cached(cpp), lines);
    QVERIFY(!highlighter.highlight(unknown));
    QVERIFY(!highlighter.cached(unknown));
    // The language's definition is remembered, not looked up again.
    QVERIFY(highlighter.highlight(*doc->blocks[2]));
    highlighter.clear();
    QVERIFY(!highlighter.cached(cpp));

    // A block with no language at the address of one highlighted earlier (a
    // new document's block where a freed one was) is plain, as highlight()
    // already said.
    QVERIFY(highlighter.highlight(cpp));
    doc->blocks[0]->language.clear();
    QVERIFY(!highlighter.highlight(cpp));
    QVERIFY(!highlighter.cached(cpp));
}

QTEST_MAIN(TestLayout)
#include "tst_layout.moc"
