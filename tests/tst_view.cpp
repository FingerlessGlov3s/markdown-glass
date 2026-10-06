#include "model/parser.h"
#include "testsupport.h"
#include "ui/documentpane.h"
#include "view/documentview.h"

#include <QApplication>
#include <QClipboard>
#include <QMouseEvent>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace md;

// Drives the real view widget with synthetic mouse and key events.
class TestView : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup() { m_view.reset(); }
    void codeBlockCopyButton();
    void inlineCodeCopyButton();
    void dragSelectAndCopy();
    void linkClick();
    void detailsToggle();
    void detailsCloseAgainOnReload();
    void findAndAnchors();
    void findBackwardsWithoutCurrentMatch();
    void findKeepsLastMatchWhenFewerRemain();
    void lostReleaseEndsDrag();
    void selectionSurvivesSameShapedRelayout();
    void widthLimitAndWrap();
    void markdownLinksInNewTabSetting();

private:
    QPoint toViewport(const QPointF &doc) const { return m_view->toViewport(doc); }
    void load(const QByteArray &markdown) { m_view->setDocument(parseMarkdown(markdown)); }
    void hover(const QPoint &pos)
    {
        // Two moves: hovering is edge-triggered on real pointer motion.
        QTest::mouseMove(m_view->viewport(), pos - QPoint(1, 0));
        QTest::mouseMove(m_view->viewport(), pos);
    }

    std::unique_ptr<DocumentView> m_view;
};

void TestView::init()
{
    m_view = std::make_unique<DocumentView>();
    m_view->resize(900, 600);
    m_view->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_view.get()));
    QApplication::clipboard()->clear();
}

void TestView::codeBlockCopyButton()
{
    load("Intro\n\n```bash\necho one\necho two\n```\n");
    const Layout *l = m_view->documentLayout();
    QCOMPARE(l->codeAreas.size(), size_t(1));
    const QRectF area = l->codeAreas[0].rect;

    hover(toViewport(area.center()));
    const qreal size = l->theme().em(1.9), inset = l->theme().em(0.45);
    const QPoint button = toViewport(QPointF(area.right() - inset - size / 2, area.top() + inset + size / 2));
    hover(button);
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, button);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("echo one\necho two"));
    QVERIFY(!m_view->hasSelection());
}

void TestView::inlineCodeCopyButton()
{
    load("Run `make install` now.\n");
    const Layout *l = m_view->documentLayout();
    const TextBox &box = *l->texts[0];
    const int start = box.layout.text().indexOf(QStringLiteral("make"));
    const QRectF code = l->toDocument(box, box.rangeRects(start, 12, LineBox::Glyphs).first());

    hover(toViewport(code.center()));
    const qreal size = qMax(l->theme().em(1.15), code.height());
    const QPoint button = toViewport(QPointF(code.right() + l->theme().em(0.25) + size / 2, code.center().y()));
    hover(button);
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, button);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("make install"));
}

void TestView::dragSelectAndCopy()
{
    load("alpha beta gamma\n\nsecond paragraph\n");
    const Layout *l = m_view->documentLayout();
    const TextBox &first = *l->texts[0];
    const TextBox &second = *l->texts[1];
    const QPoint from =
        toViewport(l->toDocument(first, first.rangeRects(6, 1, LineBox::Glyphs).first()).topLeft() + QPointF(0, 4));
    const QPoint to =
        toViewport(l->toDocument(second, second.rangeRects(5, 1, LineBox::Glyphs).first()).topRight() + QPointF(0, 4));

    QTest::mousePress(m_view->viewport(), Qt::LeftButton, {}, from);
    QTest::mouseMove(m_view->viewport(), (from + to) / 2);
    QTest::mouseMove(m_view->viewport(), to);
    QTest::mouseRelease(m_view->viewport(), Qt::LeftButton, {}, to);
    QVERIFY(m_view->hasSelection());
    QCOMPARE(m_view->selectedText(), QStringLiteral("beta gamma\n\nsecond"));

    QTest::keyClick(m_view.get(), Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("beta gamma\n\nsecond"));

    QTest::keyClick(m_view.get(), Qt::Key_A, Qt::ControlModifier);
    QCOMPARE(m_view->selectedText(), QStringLiteral("alpha beta gamma\n\nsecond paragraph"));

    // Double-click selects a word.
    QTest::mouseDClick(m_view->viewport(), Qt::LeftButton, {}, from + QPoint(4, 0));
    QCOMPARE(m_view->selectedText(), QStringLiteral("beta"));
    // QtTest keeps reporting the button as held after a synthetic double-click;
    // an explicit release resets that for the tests that follow.
    QTest::mouseRelease(m_view->viewport(), Qt::LeftButton, {}, from + QPoint(4, 0));
}

void TestView::linkClick()
{
    load("See [the docs](docs/guide.md#start) please.\n");
    const Layout *l = m_view->documentLayout();
    const TextBox &box = *l->texts[0];
    const int start = box.layout.text().indexOf(QStringLiteral("the docs"));
    const QPoint link = toViewport(l->toDocument(box, box.rangeRects(start, 8, LineBox::Glyphs).first()).center());

    QSignalSpy hovered(m_view.get(), &DocumentView::linkHovered);
    QSignalSpy activated(m_view.get(), &DocumentView::linkActivated);
    hover(link);
    QVERIFY(!hovered.isEmpty());
    QCOMPARE(hovered.last().at(0).toString(), QStringLiteral("docs/guide.md#start"));
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, link);
    QCOMPARE(activated.size(), 1);
    QCOMPARE(activated[0].at(0).toString(), QStringLiteral("docs/guide.md#start"));
    QCOMPARE(activated[0].at(1).toBool(), false);
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, Qt::ControlModifier, link);
    QCOMPARE(activated.last().at(1).toBool(), true);

    // A live reload between press and release drops the press: the link
    // belonged to the old document.
    const qsizetype before = activated.size();
    QTest::mousePress(m_view->viewport(), Qt::LeftButton, {}, link);
    load("See [other](other.md) please.\n");
    QTest::mouseRelease(m_view->viewport(), Qt::LeftButton, {}, link);
    QCOMPARE(activated.size(), before);
}

void TestView::detailsToggle()
{
    load("<details>\n<summary>More</summary>\n\nHidden body.\n\n</details>\n");
    QCOMPARE(m_view->documentLayout()->texts.size(), size_t(1));
    const QPoint summary = toViewport(m_view->documentLayout()->toggles[0].rect.center());
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, summary);
    QCOMPARE(m_view->documentLayout()->texts.size(), size_t(2));
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, summary);
    QCOMPARE(m_view->documentLayout()->texts.size(), size_t(1));
}

void TestView::detailsCloseAgainOnReload()
{
    const QByteArray source = "<details>\n<summary>More</summary>\n\nHidden body.\n\n</details>\n";
    load(source);
    const QPoint summary = toViewport(m_view->documentLayout()->toggles[0].rect.center());
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, summary);
    QCOMPARE(m_view->documentLayout()->texts.size(), size_t(2));
    // The opened block belonged to the old document; the reloaded one starts closed.
    m_view->setDocument(parseMarkdown(source), true);
    QCOMPARE(m_view->documentLayout()->texts.size(), size_t(1));
}

void TestView::findBackwardsWithoutCurrentMatch()
{
    const QByteArray source = "needle one\n\nneedle two\n\nneedle three\n";
    load(source);
    QCOMPARE(m_view->find(QStringLiteral("needle"), false), 3);
    QCOMPARE(m_view->currentMatch(), 0);
    // A reload keeps the search but has no current match yet...
    m_view->setDocument(parseMarkdown(source), true);
    QCOMPARE(m_view->matchCount(), 3);
    QCOMPARE(m_view->currentMatch(), -1);
    // ...so "previous" lands on the last match, not the one before it.
    m_view->findNext(FindDirection::Backward);
    QCOMPARE(m_view->currentMatch(), 2);
    m_view->setDocument(parseMarkdown(source), true);
    m_view->findNext(FindDirection::Forward);
    QCOMPARE(m_view->currentMatch(), 0);
}

void TestView::findKeepsLastMatchWhenFewerRemain()
{
    load("needle one\n\n<details>\n<summary>More</summary>\n\nneedle two needle three\n\n</details>\n");
    const QPoint summary = toViewport(m_view->documentLayout()->toggles[0].rect.center());
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, summary);
    QCOMPARE(m_view->find(QStringLiteral("needle"), false), 3);
    m_view->findNext(FindDirection::Forward);
    m_view->findNext(FindDirection::Forward);
    QCOMPARE(m_view->currentMatch(), 2);
    // Closing the block hides two matches: the current one moves to the last that is left.
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, summary);
    QCOMPARE(m_view->matchCount(), 1);
    QCOMPARE(m_view->currentMatch(), 0);
}

void TestView::lostReleaseEndsDrag()
{
    load("alpha beta gamma\n\nsecond paragraph\n");
    const Layout *l = m_view->documentLayout();
    const TextBox &first = *l->texts[0];
    const TextBox &second = *l->texts[1];
    const QPoint from =
        toViewport(l->toDocument(first, first.rangeRects(6, 1, LineBox::Glyphs).first()).topLeft() + QPointF(0, 4));
    const QPoint mid =
        toViewport(l->toDocument(first, first.rangeRects(11, 1, LineBox::Glyphs).first()).topLeft() + QPointF(0, 4));
    const QPoint to =
        toViewport(l->toDocument(second, second.rangeRects(5, 1, LineBox::Glyphs).first()).topRight() + QPointF(0, 4));

    QTest::mousePress(m_view->viewport(), Qt::LeftButton, {}, from);
    QTest::mouseMove(m_view->viewport(), mid);
    QCOMPARE(m_view->selectedText(), QStringLiteral("beta "));

    // The button was released while something else (a popup) had the
    // pointer: the view sees motion with no button held, and the drag ends.
    QMouseEvent noButtons(QEvent::MouseMove, QPointF(to), m_view->viewport()->mapToGlobal(QPointF(to)), Qt::NoButton,
                          Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(m_view->viewport(), &noButtons);
    QCOMPARE(m_view->selectedText(), QStringLiteral("beta "));
    // Later motion with the button held again does not continue the old drag.
    QTest::mouseMove(m_view->viewport(), to);
    QCOMPARE(m_view->selectedText(), QStringLiteral("beta "));
    QTest::mouseRelease(m_view->viewport(), Qt::LeftButton, {}, to);
}

void TestView::selectionSurvivesSameShapedRelayout()
{
    load("alpha beta\n\n<details>\n<summary>More</summary>\n\nHidden body.\n\n</details>\n");
    QCOMPARE(m_view->documentLayout()->texts.size(), size_t(2));
    m_view->selectAll();
    const QString selected = m_view->selectedText();
    QVERIFY(selected.startsWith(QStringLiteral("alpha beta")));

    // Zooming re-lays out the same boxes: the selection means the same text.
    m_view->setZoom(1.5);
    QCOMPARE(m_view->selectedText(), selected);
    m_view->setWidthLimit(true, 400);
    QCOMPARE(m_view->selectedText(), selected);

    // Opening the details block adds a box, so the indices are meaningless.
    const QPoint summary = toViewport(m_view->documentLayout()->toggles[0].rect.center());
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, summary);
    QCOMPARE(m_view->documentLayout()->texts.size(), size_t(3));
    QVERIFY(!m_view->hasSelection());
}

void TestView::findAndAnchors()
{
    QByteArray source = "# Top\n\n";
    for (int i = 0; i < 80; ++i)
        source += "Filler paragraph number " + QByteArray::number(i) + ".\n\n";
    source += "## Far Away\n\nThe needle is here. Another Needle too.\n";
    load(source);

    QCOMPARE(m_view->verticalScrollBar()->value(), 0);
    QCOMPARE(m_view->find(QStringLiteral("needle"), false), 2);
    QCOMPARE(m_view->currentMatch(), 0);
    QVERIFY(m_view->verticalScrollBar()->value() > 500);
    m_view->findNext(FindDirection::Forward);
    QCOMPARE(m_view->currentMatch(), 1);
    m_view->findNext(FindDirection::Forward);
    QCOMPARE(m_view->currentMatch(), 0);
    QCOMPARE(m_view->find(QStringLiteral("needle"), true), 1);
    QCOMPARE(m_view->find(QStringLiteral("absent"), false), 0);

    m_view->verticalScrollBar()->setValue(0);
    QCOMPARE(m_view->currentHeading(), m_view->document()->blocks[0].get());
    QVERIFY(m_view->scrollToAnchor(QStringLiteral("far-away")));
    QVERIFY(m_view->verticalScrollBar()->value() > 500);
    QCOMPARE(m_view->currentHeading()->anchor, QStringLiteral("far-away"));
    QVERIFY(!m_view->scrollToAnchor(QStringLiteral("nope")));
}

void TestView::widthLimitAndWrap()
{
    load(QByteArray("word ").repeated(100) + "\n\n```\n" + QByteArray("code ").repeated(100) + "\n```\n");
    m_view->setWidthLimit(true, 400);
    QCOMPARE(m_view->documentLayout()->width(), 400.0);
    m_view->setWidthLimit(false, 400);
    QVERIFY(m_view->documentLayout()->width() > 700);

    QVERIFY(m_view->documentLayout()->frames.empty());
    m_view->setWrapCode(false);
    QCOMPARE(m_view->documentLayout()->frames.size(), size_t(1));
    // The code block's own scrollbar is shown and drives the frame offset.
    const auto bars = m_view->viewport()->findChildren<QScrollBar *>();
    QCOMPARE(bars.size(), 1);
    QVERIFY(bars[0]->isVisible());
    bars[0]->setValue(120);
    QCOMPARE(m_view->documentLayout()->frames[0].offset, 120.0);
    m_view->setWrapCode(true);
    QVERIFY(!bars[0]->isVisible());
}

void TestView::markdownLinksInNewTabSetting()
{
    QTemporaryDir dir;
    for (const char *name : {"a.md", "b.md"})
        writeFile(dir.path(), QLatin1String(name), "# Title\n\n[other](b.md)\n");
    DocumentPane pane;
    QVERIFY(pane.load(dir.filePath(QStringLiteral("a.md"))));
    QSignalSpy newTab(&pane, &DocumentPane::openInNewTab);

    Settings s;
    s.linksInNewTab = true;
    pane.applySettings(s);
    emit pane.view()->linkActivated(QStringLiteral("b.md"), OpenIn::CurrentTab);
    QCOMPARE(newTab.size(), 1);
    QCOMPARE(pane.path(), dir.filePath(QStringLiteral("a.md")));

    s.linksInNewTab = false;
    pane.applySettings(s);
    emit pane.view()->linkActivated(QStringLiteral("b.md"), OpenIn::CurrentTab);
    QCOMPARE(newTab.size(), 1);
    QCOMPARE(pane.path(), dir.filePath(QStringLiteral("b.md")));
    QVERIFY(pane.canGoBack());
}

QTEST_MAIN(TestView)
#include "tst_view.moc"
