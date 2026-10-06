#include "model/htmlsubset.h"
#include "model/parser.h"
#include "model/regularfile.h"
#include "model/textcheck.h"

#include <QTemporaryDir>
#include <QTest>

#include <sys/stat.h>

using namespace md;

class TestParser : public QObject
{
    Q_OBJECT
private slots:
    void textCheck_data();
    void textCheck();
    void headingsAndSlugs();
    void inlineFormatting();
    void codeBlockLanguage();
    void listsAndTasks();
    void tables();
    void footnotes();
    void htmlDetailsAndAlign();
    void htmlImagesAndInlineTags();
    void htmlBlockStructure();
    void scriptsAreDropped();
    void tokenizerSurvivesMalformedInput();
    void entities();
    void remoteImagesDetected();
    void nestingIsBounded();
    void onlyRegularFilesOpen();
};

static const Block &at(const Document &doc, size_t i)
{
    return *doc.blocks.at(i);
}

void TestParser::headingsAndSlugs()
{
    const auto doc = parseMarkdown("# Hello, World!\n\n## Setup\n\n## Setup\n\n### C++ & Qt_6\n");
    QCOMPARE(doc->blocks.size(), size_t(4));
    QCOMPARE(at(*doc, 0).type, Block::Heading);
    QCOMPARE(at(*doc, 0).level, 1);
    QCOMPARE(at(*doc, 0).anchor, QStringLiteral("hello-world"));
    QCOMPARE(at(*doc, 1).anchor, QStringLiteral("setup"));
    QCOMPARE(at(*doc, 2).anchor, QStringLiteral("setup-1"));
    QCOMPARE(at(*doc, 3).anchor, QStringLiteral("c--qt_6"));
    QCOMPARE(doc->outline.size(), 4);

    // Letters outside the basic plane (surrogate pairs in UTF-16) stay in the
    // anchor, lowercased: U+10400 DESERET CAPITAL LONG I becomes U+10428.
    const auto astral = parseMarkdown("# A \xF0\x90\x90\x80 b \xF0\x9D\x92\x9C\n");
    const char32_t expected[] = {U'a', U'-', U'\x10428', U'-', U'b', U'-', U'\x1D49C'};
    QCOMPARE(at(*astral, 0).anchor, QString::fromUcs4(expected, 7));
    QCOMPARE(doc->outline.at(3).level, 3);
    QCOMPARE(doc->findAnchor(QStringLiteral("setup-1")), &at(*doc, 2));
}

void TestParser::inlineFormatting()
{
    const auto doc = parseMarkdown("plain **bold** `code` [link](http://x.test) ~~gone~~\n");
    const InlineText &inl = at(*doc, 0).inl;
    QCOMPARE(inl.text, QStringLiteral("plain bold code link gone"));
    QCOMPARE(inl.links, QStringList {QStringLiteral("http://x.test")});

    auto flagsAt = [&](const QString &word) {
        const int pos = inl.text.indexOf(word);
        for (const Span &s : inl.spans) {
            if (pos >= s.start && pos < s.start + s.length)
                return s;
        }
        return Span();
    };
    QVERIFY(flagsAt(QStringLiteral("bold")).flags & Span::Bold);
    QVERIFY(flagsAt(QStringLiteral("code")).flags & Span::Code);
    QVERIFY(flagsAt(QStringLiteral("gone")).flags & Span::Strike);
    QCOMPARE(flagsAt(QStringLiteral("link")).link, 0);
    QCOMPARE(flagsAt(QStringLiteral("plain")).flags, Span::Flags());
}

void TestParser::codeBlockLanguage()
{
    const auto doc = parseMarkdown("```\nplain\n```\n\n```bash title=x\necho hi\n```\n\n    indented\n");
    QCOMPARE(at(*doc, 0).type, Block::CodeBlock);
    QVERIFY(at(*doc, 0).language.isEmpty());
    QCOMPARE(at(*doc, 0).code, QStringLiteral("plain"));
    QCOMPARE(at(*doc, 1).language, QStringLiteral("bash"));
    QCOMPARE(at(*doc, 1).code, QStringLiteral("echo hi"));
    QVERIFY(at(*doc, 2).language.isEmpty());
}

void TestParser::listsAndTasks()
{
    const auto doc = parseMarkdown("3. three\n4. four\n\n- [x] done\n- [ ] todo\n- plain\n");
    const Block &ordered = at(*doc, 0);
    QCOMPARE(ordered.type, Block::List);
    QVERIFY(ordered.ordered);
    QCOMPARE(ordered.listStart, 3);
    const Block &tasks = at(*doc, 1);
    QCOMPARE(tasks.children.size(), size_t(3));
    QCOMPARE(tasks.children[0]->task, Task::Done);
    QCOMPARE(tasks.children[1]->task, Task::Open);
    QCOMPARE(tasks.children[2]->task, Task::None);
}

void TestParser::tables()
{
    const auto doc = parseMarkdown("| a | b | c |\n|:--|:-:|--:|\n| 1 | **2** | 3 |\n");
    const Block &table = at(*doc, 0);
    QCOMPARE(table.type, Block::Table);
    QCOMPARE(table.columns, (QList<Align> {Align::Left, Align::Center, Align::Right}));
    QCOMPARE(table.rows.size(), 2);
    QCOMPARE(table.rows[1][1].text, QStringLiteral("2"));
    QVERIFY(table.rows[1][1].spans.first().flags & Span::Bold);
}

void TestParser::footnotes()
{
    const auto doc = parseMarkdown("Text[^note].\n\n[^note]: The note.\n");
    const InlineText &inl = at(*doc, 0).inl;
    QCOMPARE(inl.links, QStringList {QStringLiteral("#fn-1")});
    const Block &footnote = *doc->blocks.back();
    QCOMPARE(footnote.type, Block::Footnote);
    QCOMPARE(footnote.number, 1);
    QCOMPARE(doc->findAnchor(QStringLiteral("fn-1")), &footnote);
}

void TestParser::htmlDetailsAndAlign()
{
    const auto doc =
        parseMarkdown("<details open>\n<summary>More <b>info</b></summary>\n\nInside **markdown**.\n\n</details>\n\n"
                      "<div align=\"center\">\n\n# Centred\n\n</div>\n\nAfter.\n");
    const Block &details = at(*doc, 0);
    QCOMPARE(details.type, Block::Details);
    QVERIFY(details.open);
    QCOMPARE(details.inl.text, QStringLiteral("More info"));
    QCOMPARE(details.children.size(), size_t(1));
    QCOMPARE(details.children[0]->inl.text, QStringLiteral("Inside markdown."));

    QCOMPARE(at(*doc, 1).type, Block::Heading);
    QCOMPARE(at(*doc, 1).align, Align::Center);
    QCOMPARE(at(*doc, 2).align, Align::Default);
}

void TestParser::htmlImagesAndInlineTags()
{
    const auto doc = parseMarkdown(
        "<p align=\"center\"><a href=\"https://x.test\"><img src=\"logo.png\" width=\"120\" alt=\"Logo\"></a></p>\n\n"
        "Press <kbd>Ctrl</kbd> and H<sub>2</sub>O<br>next\n");
    const Block &p = at(*doc, 0);
    QCOMPARE(p.align, Align::Center);
    QCOMPARE(p.inl.images.size(), 1);
    QCOMPARE(p.inl.images[0].src, QStringLiteral("logo.png"));
    QCOMPARE(p.inl.images[0].width, 120);
    QCOMPARE(p.inl.images[0].alt, QStringLiteral("Logo"));
    QCOMPARE(p.inl.links.value(p.inl.images[0].link), QStringLiteral("https://x.test"));

    const InlineText &inl = at(*doc, 1).inl;
    QVERIFY(inl.text.contains(QChar::LineSeparator));
    bool kbd = false, sub = false;
    for (const Span &s : inl.spans) {
        kbd |= bool(s.flags & Span::Kbd);
        sub |= bool(s.flags & Span::Sub);
    }
    QVERIFY(kbd && sub);

    // An <a> without href, or a stray </a>, does not end the enclosing link.
    const auto nested = parseMarkdown("[x <a>y</a> z](https://u.test) </a>after\n");
    const InlineText &link = at(*nested, 0).inl;
    auto linkAt = [&](const QString &word) {
        const int pos = link.text.indexOf(word);
        for (const Span &s : link.spans) {
            if (pos >= s.start && pos < s.start + s.length)
                return s.link;
        }
        return -1;
    };
    QCOMPARE(linkAt(QStringLiteral("z")), 0);
    QCOMPARE(linkAt(QStringLiteral("after")), -1);
}

// Block-level tags in a raw HTML block: headings, rules, alignment
// containers and tags that only end the paragraph.
void TestParser::htmlBlockStructure()
{
    const auto doc = parseMarkdown("<div>\n"
                                   "<h2 align=\"right\">Raw <b>title</b></h2>\n"
                                   "<center>middle</center>\n"
                                   "<ul><li>one</li><li>two</li></ul>\n"
                                   "<hr>\n"
                                   "<table><tr><td>a</td><th>b</th></tr></table>\n"
                                   "</details> after\n"
                                   "</div>\n");
    QList<const Block *> blocks;
    for (const auto &b : doc->blocks)
        blocks.append(b.get());
    QVERIFY(blocks.size() >= 5);

    const Block &heading = *blocks[0];
    QCOMPARE(heading.type, Block::Heading);
    QCOMPARE(heading.level, 2);
    QCOMPARE(heading.align, Align::Right);
    QCOMPARE(heading.inl.text, QStringLiteral("Raw title"));
    QCOMPARE(heading.anchor, QStringLiteral("raw-title"));
    QCOMPARE(doc->outline.size(), 1);

    QCOMPARE(blocks[1]->type, Block::Paragraph);
    QCOMPARE(blocks[1]->align, Align::Center);
    QCOMPARE(blocks[1]->inl.text, QStringLiteral("middle"));

    // List items end paragraphs but are not rendered as a list.
    QCOMPARE(blocks[2]->inl.text, QStringLiteral("one"));
    QCOMPARE(blocks[3]->inl.text, QStringLiteral("two"));

    bool rule = false, cells = false, after = false;
    for (const Block *b : std::as_const(blocks)) {
        rule |= b->type == Block::Rule;
        cells |= b->inl.text.simplified() == QStringLiteral("a b");
        after |= b->inl.text.trimmed() == QStringLiteral("after"); // a stray </details> is ignored
    }
    QVERIFY(rule);
    QVERIFY(cells);
    QVERIFY(after);
}

void TestParser::scriptsAreDropped()
{
    const auto doc =
        parseMarkdown("<script>alert('x')</script>\n\n<div onclick=\"evil()\">shown<style>p{}</style></div>\n\n"
                      "[js](javascript:alert(1))\n");
    QString all;
    for (const auto &b : doc->blocks)
        all += b->inl.text + u'\n';
    QVERIFY(!all.contains(QStringLiteral("alert('x')")));
    QVERIFY(!all.contains(QStringLiteral("p{}")));
    QVERIFY(all.contains(QStringLiteral("shown")));
}

void TestParser::tokenizerSurvivesMalformedInput()
{
    const QStringList inputs = {
        QStringLiteral("<"),
        QStringLiteral("<<<>>>"),
        QStringLiteral("<a href="),
        QStringLiteral("<a href=\"x"),
        QStringLiteral("</"),
        QStringLiteral("<!--"),
        QStringLiteral("<!"),
        QStringLiteral("<img src=x onerror=y"),
        QStringLiteral("a < b > c"),
        QStringLiteral("<script>never closed"),
        QStringLiteral("&#xFFFFFFFF; &#0; &bogus; &"),
        QStringLiteral("<p a=1 a=2 =3 / >text</p >"),
    };
    for (const QString &input : inputs)
        tokenizeHtml(input); // must not crash or hang

    const auto tokens = tokenizeHtml(QStringLiteral("a < b <b>bold</b>"));
    QCOMPARE(tokens.first().kind, HtmlToken::Text);
    QCOMPARE(tokens.first().text, QStringLiteral("a < b "));
    QCOMPARE(tokens.at(1).name, QStringLiteral("b"));

    const auto img = tokenizeHtml(QStringLiteral("<IMG SRC='a.png' Width=40 alt=\"x &amp; y\">"));
    QCOMPARE(img.size(), 1);
    QCOMPARE(img[0].name, QStringLiteral("img"));
    QCOMPARE(img[0].attrs.value(QStringLiteral("src")), QStringLiteral("a.png"));
    QCOMPARE(img[0].attrs.value(QStringLiteral("width")), QStringLiteral("40"));
    QCOMPARE(img[0].attrs.value(QStringLiteral("alt")), QStringLiteral("x & y"));
}

void TestParser::entities()
{
    QCOMPARE(decodeHtmlEntities(u"&lt;a&gt; &amp; &#65;&#x42; &nbsp;&unknown; &"),
             QStringLiteral("<a> & AB  &unknown; &"));
}

void TestParser::remoteImagesDetected()
{
    QVERIFY(!isRemoteUrl(QStringLiteral("local.png")));
    QVERIFY(!isRemoteUrl(QStringLiteral("file:///tmp/a.png")));
    QVERIFY(isRemoteUrl(QStringLiteral("https://x.test/a.png")));
    QVERIFY(isRemoteUrl(QStringLiteral("//x.test/a.png")));
    // Images in tables are found like any other.
    const auto doc = parseMarkdown("| a |\n|---|\n| <img src=\"//x.test/a.png\"> |\n");
    QCOMPARE(doc->blocks.at(0)->rows.at(1).at(0).images.value(0).src, QStringLiteral("//x.test/a.png"));
}

static int depthOf(const std::vector<std::unique_ptr<Block>> &blocks)
{
    int deepest = 0;
    for (const auto &b : blocks)
        deepest = qMax(deepest, 1 + depthOf(b->children));
    return deepest;
}

void TestParser::nestingIsBounded()
{
    const auto quotes = parseMarkdown(QByteArray(5000, '>') + " deep\n");
    QVERIFY(depthOf(quotes->blocks) <= 70);

    QByteArray details;
    for (int i = 0; i < 5000; ++i)
        details += "<details>\n\n";
    details += "x\n";
    QVERIFY(depthOf(parseMarkdown(details)->blocks) <= 70);

    QByteArray emphasis;
    for (int i = 0; i < 3000; ++i)
        emphasis += "*a **b ";
    parseMarkdown(emphasis); // must not overflow the stack
}

void TestParser::textCheck_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<int>("expected");
    const int text = int(NotText::No);

    QTest::newRow("markdown") << QByteArray("# Title\n\nSome *text* with `code`.\n") << text;
    QTest::newRow("empty") << QByteArray() << text;
    QTest::newRow("utf-8 bom") << QByteArray("\xEF\xBB\xBF# Hi\n") << text;
    QTest::newRow("utf-8 accents") << QByteArray("Caf\xC3\xA9, na\xC3\xAFve, \xE6\x97\xA5\xE6\x9C\xAC\n") << text;
    QTest::newRow("a few latin-1 bytes") << QByteArray("Caf\xE9 au lait, and more ordinary text here.\n").repeated(20)
                                         << text;
    QTest::newRow("terminal colours") << QByteArray("\x1b[31mred\x1b[0m\tand tabs\r\n\f") << text;
    QTest::newRow("real U+FFFD") << QByteArray("\xEF\xBF\xBD").repeated(50) << text;

    QTest::newRow("nul byte") << QByteArray("text\0more", 9) << int(NotText::NulBytes);
    QTest::newRow("png") << QByteArray("\x89PNG\r\n\x1a\n\0\0\0\rIHDR", 16) << int(NotText::NulBytes);
    QTest::newRow("binary stl") << QByteArray("STLB ATF 12.14.0.127 COLOR=\xF0\xF0\xF0\xFF  \0\0\x01\0", 36)
                                << int(NotText::NulBytes);
    QTest::newRow("utf-16") << QByteArray("\xFF\xFE#\0 \0H\0i\0", 10) << int(NotText::NulBytes);
    // (Literal pieces kept separate: "\x01b" would read as one hex escape.)
    QTest::newRow("control characters") << QByteArray("a\x01"
                                                      "x\x02"
                                                      "x\x03"
                                                      "x\x04"
                                                      "x\x05"
                                                      "x\x06"
                                                      "x\x07")
                                        << int(NotText::ControlCharacters);
    QByteArray noise;
    for (int i = 0; i < 4096; ++i)
        noise += char(0x80 + (i * 37) % 0x7F); // high bytes in no valid UTF-8 order
    QTest::newRow("invalid utf-8") << noise << int(NotText::InvalidUtf8);
}

void TestParser::textCheck()
{
    QFETCH(QByteArray, data);
    QFETCH(int, expected);
    QCOMPARE(int(checkText(data)), expected);
}

void TestParser::onlyRegularFilesOpen()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString regular = dir.filePath(QStringLiteral("a.md"));
    {
        QFile f(regular);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("# A\n");
    }
    QFile ok(regular);
    QVERIFY(openRegularFile(ok));
    QCOMPARE(ok.readAll(), QByteArray("# A\n"));

    // A FIFO must be refused without blocking: nothing ever writes to it.
    const QString fifo = dir.filePath(QStringLiteral("fifo.md"));
    QCOMPARE(::mkfifo(qPrintable(fifo), 0600), 0);
    for (const QString &path :
         {fifo, dir.path(), dir.filePath(QStringLiteral("missing.md")), QStringLiteral("/dev/zero")}) {
        QFile file(path);
        QString error;
        QVERIFY2(!openRegularFile(file, &error), qPrintable(path));
        QVERIFY(!error.isEmpty());
        QVERIFY(!file.isOpen());
    }
}

QTEST_APPLESS_MAIN(TestParser)
#include "tst_parser.moc"
