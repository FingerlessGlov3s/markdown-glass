#include "app/links.h"
#include "highlight/highlighter.h"
#include "images/imageloader.h"
#include "layout/layout.h"
#include "model/parser.h"
#include "print/printing.h"
#include "testsupport.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <sys/stat.h>

using namespace md;

// Documents built to stress the viewer. Each must parse, lay out and paint
// without crashing, and within a generous time bound.
class TestHostile : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void documents_data();
    void documents();
    void links_data();
    void links();

private:
    QTemporaryDir m_dir;
};

// Files next to the hostile documents, for their images to point at.
void TestHostile::initTestCase()
{
    QVERIFY(m_dir.isValid());
    writeFile(m_dir.path(), QStringLiteral("external.svg"),
              "<svg xmlns='http://www.w3.org/2000/svg' width='9' height='9'>"
              "<image href='/etc/passwd' width='9' height='9'/></svg>");
    writeFile(m_dir.path(), QStringLiteral("tall.svg"),
              "<svg xmlns='http://www.w3.org/2000/svg' width='1' height='99999999'/>");
    writeFile(m_dir.path(), QStringLiteral("fake.png"), QByteArray("\x89PNG\r\n\x1a\n") + QByteArray(64, '\xff'));
    // Thirty nested doublings: a billion rectangles from a few kilobytes.
    QByteArray fanout =
        "<svg xmlns='http://www.w3.org/2000/svg' width='9' height='9'><rect id='a0' width='1' height='1'/>";
    for (int i = 1; i <= 30; ++i) {
        const QByteArray previous = "#a" + QByteArray::number(i - 1);
        fanout +=
            "<g id='a" + QByteArray::number(i) + "'><use href='" + previous + "'/><use href='" + previous + "'/></g>";
    }
    writeFile(m_dir.path(), QStringLiteral("fanout.svg"), fanout + "<use href='#a30'/></svg>");
    QCOMPARE(::mkfifo(qPrintable(m_dir.filePath(QStringLiteral("fifo.png"))), 0600), 0);
}

void TestHostile::documents_data()
{
    QTest::addColumn<QByteArray>("source");

    QTest::newRow("empty") << QByteArray();
    QTest::newRow("nul bytes") << QByteArray(4096, '\0');
    QTest::newRow("invalid utf8") << QByteArray("# \xff\xfe\xfd title\n\n\xc3\x28 text \xf0\x28\x8c\x28\n");
    QTest::newRow("deep quotes") << QByteArray(20000, '>') + " x\n";

    QByteArray lists;
    for (int i = 0; i < 400; ++i)
        lists += QByteArray(i * 2, ' ') + "- item\n";
    QTest::newRow("deep lists") << lists;

    QByteArray table = "|" + QByteArray("a|").repeated(300) + "\n|" + QByteArray("-|").repeated(300) + "\n";
    for (int r = 0; r < 60; ++r)
        table += "|" + QByteArray("cell|").repeated(300) + "\n";
    QTest::newRow("huge table") << table;
    // Wide and tall at once: padding every row to the header's width would
    // need billions of cells.
    QByteArray wideTall = "|" + QByteArray("a|").repeated(20000) + "\n|" + QByteArray("-|").repeated(20000) + "\n";
    wideTall += QByteArray("|a|\n").repeated(3000);
    QTest::newRow("wide and tall table") << wideTall;

    QTest::newRow("long line") << QByteArray(300000, 'a') + "\n";
    // One word: no break opportunities, which Qt's usual wrapping handles in
    // quadratic time.
    QTest::newRow("long word") << QByteArray(2'000'000, 'a') + "\n";
    QTest::newRow("long code line") << "```cpp\n" + QByteArray("int x = 1; ").repeated(20000) + "\n```\n";
    QTest::newRow("many code lines") << "```bash\n" + QByteArray("echo $x | grep -v 'y' # comment\n").repeated(15000)
            + "```\n";
    QTest::newRow("unclosed fence") << QByteArray("```python\n") + QByteArray("def f(): pass\n").repeated(2000);
    QTest::newRow("emphasis soup") << QByteArray("*a **b _c __d ").repeated(4000) + "\n";
    QTest::newRow("link soup") << QByteArray("[a](b \"c\") ![x](y) <z@z.z> https://q.test ").repeated(3000) + "\n";
    QTest::newRow("bracket soup") << QByteArray(30000, '[') + QByteArray(30000, ']') + "\n";
    QTest::newRow("html soup")
        << QByteArray("<details><summary><b><a href='x'><img src=y width=99999999 height=-5>").repeated(3000) + "\n";
    QTest::newRow("unterminated html") << QByteArray("<div align=center <p <img src=\"") + QByteArray(50000, 'x')
            + "\n";
    QTest::newRow("script tags") << QByteArray(
        "<script>while(1){}</script>\n\n<style>*{display:none}</style>\n\n<iframe src=\"file:///etc/passwd\"></iframe>\n");
    QTest::newRow("dangerous links") << QByteArray(
        "[a](javascript:alert(1)) [b](file:///bin/sh) [c](data:text/html,x) [d](vbscript:x)\n");
    QTest::newRow("image bombs") << QByteArray(
        "![a](/dev/zero) ![b](/dev/random) ![c](file:///proc/self/mem) <img src=\"/dev/stdin\" width=20000 height=20000>\n");
    QTest::newRow("hostile images") << QByteArray(
        "![a](external.svg) ![b](tall.svg) ![c](fake.png) ![d](fifo.png) ![e](../../../../../../etc/passwd) "
        "![j](fanout.svg) "
        "![f](https://127.0.0.1/x.png) ![g](data:image/svg+xml;base64,PHN2Zz48aW1hZ2UgaHJlZj0nL2V0Yy9wYXNzd2QnLz48L3N2Zz4=) "
        "![h](data:;base64,%%%) ![i](ftp://host/x.png)\n");
    QTest::newRow("many footnotes") << QByteArray("x[^a] ").repeated(2000) + "\n\n[^a]: note\n";
    QTest::newRow("many headings") << QByteArray("# h\n").repeated(20000);
    QTest::newRow("entities") << QByteArray("&#x110000; &#99999999999; &amp;amp; &#0; &#xD800; ").repeated(2000) + "\n";
    // Raw HTML is decoded by the viewer's own entity scanner; a run of
    // ampersands with no semicolon must not be quadratic.
    QTest::newRow("ampersand run") << "<div>\n" + QByteArray(1'000'000, '&') + "\n\n<div title=\""
            + QByteArray(500'000, '&') + "\">x</div>\n";
    QTest::newRow("control chars") << QByteArray("a\x01\x02\x1b[31m\x7f\xe2\x80\xae"
                                                 "rtl override\n");
}

void TestHostile::documents()
{
    QFETCH(QByteArray, source);
    QElapsedTimer timer;
    timer.start();

    const auto doc = parseMarkdown(source);
    QVERIFY(doc);
    for (const auto &block : doc->blocks) {
        if (block->type == Block::Table) {
            QVERIFY(block->columns.size() <= 128);
            QVERIFY(qsizetype(block->rows.size()) * block->columns.size() <= 100'000);
        }
    }

    const Theme theme = Theme::fromPalette(QGuiApplication::palette(), 1.0);
    CodeHighlighter highlighter;
    // The real image loader, with remote images off as they are by default.
    // A first layout requests every image; the loads are given time to finish
    // so the layouts below also see the decoded (or refused) results.
    ImageLoader images;
    images.setBaseDir(m_dir.path());
    if (source.contains("![") || source.contains("<img")) {
        QSignalSpy loaded(&images, &ImageLoader::changed);
        LayoutOptions options;
        options.width = 700;
        const Layout first(*doc, theme, options, &highlighter, &images);
        loaded.wait(3000);
    }
    for (bool wrap : {true, false}) {
        LayoutOptions options;
        options.width = 700;
        options.wrapCode = wrap;
        options.expandAllDetails = true;
        const Layout layout(*doc, theme, options, &highlighter, &images);
        QVERIFY(layout.height() >= 0);

        QImage image(700, 900, QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter p(&image);
        layout.paint(p, QRectF(0, 0, 700, 900), PaintState());
        p.end();

        layout.hitTest(QPointF(350, 450));
        if (wrap)
            QVERIFY(!paginate(layout, 1000).isEmpty());
    }
    QVERIFY2(timer.elapsed() < 60000, "took too long");
    qInfo("%s: %lld ms", QTest::currentDataTag(), timer.elapsed());
}

void TestHostile::links_data()
{
    QTest::addColumn<QString>("href");
    QTest::addColumn<int>("kind");

    const int unsupported = LinkTarget::Unsupported;
    const int otherFile = LinkTarget::OtherFile;
    QTest::newRow("javascript") << QStringLiteral("javascript:alert(1)") << unsupported;
    QTest::newRow("javascript case") << QStringLiteral("  JaVaScRiPt:alert(1)") << unsupported;
    QTest::newRow("vbscript") << QStringLiteral("vbscript:msgbox") << unsupported;
    QTest::newRow("data html") << QStringLiteral("data:text/html;base64,PHNjcmlwdD4=") << unsupported;
    QTest::newRow("smb") << QStringLiteral("smb://attacker/share") << unsupported;
    QTest::newRow("protocol relative") << QStringLiteral("//attacker/x.md") << unsupported;
    QTest::newRow("custom scheme") << QStringLiteral("steam://run/1") << unsupported;
    QTest::newRow("web without host") << QStringLiteral("http:///etc/passwd") << unsupported;
    QTest::newRow("shell script") << QStringLiteral("file:///bin/sh") << otherFile;
    QTest::newRow("traversal") << QStringLiteral("%2e%2e/%2e%2e/%2e%2e/etc/passwd") << otherFile;
    QTest::newRow("windows share") << QStringLiteral("\\\\attacker\\share\\x.exe") << otherFile;
    QTest::newRow("desktop file") << QStringLiteral("/usr/share/applications/evil.desktop") << otherFile;
    QTest::newRow("markup") << QStringLiteral("x:<img src=/dev/zero>") << unsupported;
}

// Every link a document can hold is classified, and the dangerous kinds are
// never ones the viewer acts on without asking.
void TestHostile::links()
{
    QFETCH(QString, href);
    QFETCH(int, kind);
    const LinkTarget target = resolveLink(href, m_dir.path());
    QCOMPARE(int(target.kind), kind);
}

QTEST_MAIN(TestHostile)
#include "tst_hostile.moc"
