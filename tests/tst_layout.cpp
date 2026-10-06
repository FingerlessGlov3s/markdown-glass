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

class TestLayout : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { m_theme = Theme::fromPalette(QGuiApplication::palette(), 1.0); }
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

private:
    std::unique_ptr<Layout> build(const Document &doc, qreal width, bool wrapCode = true)
    {
        LayoutOptions options;
        options.width = width;
        options.wrapCode = wrapCode;
        return std::make_unique<Layout>(doc, m_theme, options, &m_highlighter, nullptr);
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

QTEST_MAIN(TestLayout)
#include "tst_layout.moc"
