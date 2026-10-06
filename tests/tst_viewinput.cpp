#include "model/parser.h"
#include "testsupport.h"
#include "view/documentview.h"

#include <QContextMenuEvent>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>

using namespace md;

// The document view's keyboard, wheel and context-menu handling, selection
// auto-scroll, and reacting to palette changes and resizing.
class TestViewInput : public QObject
{
    Q_OBJECT
private slots:
    void cleanup();

    void viewKeyboardAndWheel();
    void viewContextMenuAndMiddleClick();
    void viewAutoScrollWhileSelecting();
    void viewFollowsPaletteAndResize();
};

void TestViewInput::cleanup()
{
    QApplication::closeAllWindows();
}

void TestViewInput::viewKeyboardAndWheel()
{
    DocumentView view;
    view.resize(800, 500);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    view.setDocument(parseMarkdown(longDocument(100, "```\n" + QByteArray("wide ").repeated(200) + "\n```\n")));
    QScrollBar *bar = view.verticalScrollBar();

    QTest::keyClick(&view, Qt::Key_End);
    QCOMPARE(bar->value(), bar->maximum());
    QTest::keyClick(&view, Qt::Key_Home);
    QCOMPARE(bar->value(), 0);
    QTest::keyClick(&view, Qt::Key_Space);
    const int page = bar->value();
    QVERIFY(page > 0);
    QTest::keyClick(&view, Qt::Key_Space, Qt::ShiftModifier);
    QCOMPARE(bar->value(), 0);
    QTest::keyClick(&view, Qt::Key_PageDown);
    QVERIFY(bar->value() > 0);

    // Ctrl+wheel zooms.
    QSignalSpy zoom(&view, &DocumentView::zoomChanged);
    QWheelEvent zoomIn(QPointF(100, 100), view.viewport()->mapToGlobal(QPointF(100, 100)), QPoint(), QPoint(0, 120),
                       Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(view.viewport(), &zoomIn);
    QCOMPARE(zoom.size(), 1);
    QVERIFY(view.zoom() > 1.0);
    view.setZoom(view.zoom()); // unchanged: no signal
    QCOMPARE(zoom.size(), 1);
    view.setZoom(1.0);

    // Shift+wheel over an unwrapped code block scrolls it sideways.
    view.setWrapCode(false);
    QVERIFY(view.scrollToAnchor(QStringLiteral("section-99")));
    QTest::keyClick(&view, Qt::Key_End);
    const ScrollFrame &frame = view.documentLayout()->frames.at(0);
    const QPointF centre = frame.rect.center();
    const QPoint at(qRound(centre.x() + (view.viewport()->width() - view.documentLayout()->width()) / 2),
                    qRound(centre.y() + view.verticalPadding() - bar->value()));
    QWheelEvent sideways(at, view.viewport()->mapToGlobal(at), QPoint(), QPoint(0, -240), Qt::NoButton,
                         Qt::ShiftModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(view.viewport(), &sideways);
    QVERIFY(view.documentLayout()->frames.at(0).offset > 0);

    // Navigation edge cases.
    QVERIFY(view.scrollToAnchor(QString()));
    QCOMPARE(bar->value(), 0);
    QVERIFY(view.scrollToAnchor(QStringLiteral("SECTION-5")));
    view.scrollToBlock(nullptr);
}

void TestViewInput::viewContextMenuAndMiddleClick()
{
    DocumentView view;
    view.resize(800, 500);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    view.setDocument(parseMarkdown("A [link](https://example.invalid) and `code`.\n\n```\nblock\n```\n"));
    const Layout *l = view.documentLayout();
    const TextBox &box = *l->texts[0];
    const int linkAt = box.layout.text().indexOf(QStringLiteral("link"));
    const QPoint link =
        view.toViewport(l->toDocument(box, box.rangeRects(linkAt, 4, LineBox::Glyphs).first()).center());
    const int codeAt = box.layout.text().indexOf(QStringLiteral("code"));
    const QPoint code =
        view.toViewport(l->toDocument(box, box.rangeRects(codeAt, 4, LineBox::Glyphs).first()).center());
    const QPoint block = view.toViewport(l->codeAreas.at(0).rect.center());

    QSignalSpy activated(&view, &DocumentView::linkActivated);
    QTest::mouseClick(view.viewport(), Qt::MiddleButton, {}, link);
    QCOMPARE(activated.size(), 1);
    QCOMPARE(activated[0].at(1).value<OpenIn>(), OpenIn::NewTab);

    // The context menu over a link, inline code and a code block; each is dismissed.
    ModalCloser closer;
    for (const QPoint &p : {link, code, block}) {
        QContextMenuEvent menu(QContextMenuEvent::Mouse, p, view.viewport()->mapToGlobal(p));
        QApplication::sendEvent(view.viewport(), &menu);
    }
    QCOMPARE(closer.closed, 3);

    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(view.viewport(), &leave);
    QApplication::sendEvent(&view, &leave);

    view.setDocument(nullptr);
    QVERIFY(!view.documentLayout());
    view.selectAll();
    view.copy();
    QVERIFY(view.selectedText().isEmpty());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, {}, QPoint(10, 10));
    QContextMenuEvent none(QContextMenuEvent::Mouse, QPoint(10, 10), view.viewport()->mapToGlobal(QPoint(10, 10)));
    QApplication::sendEvent(view.viewport(), &none);
}

void TestViewInput::viewAutoScrollWhileSelecting()
{
    DocumentView view;
    view.resize(800, 400);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    view.setDocument(parseMarkdown(longDocument(100)));
    QScrollBar *bar = view.verticalScrollBar();

    // Dragging past the bottom edge keeps scrolling and extending the selection.
    QTest::mousePress(view.viewport(), Qt::LeftButton, {}, QPoint(100, 60));
    QTest::mouseMove(view.viewport(), QPoint(100, 200));
    QTest::mouseMove(view.viewport(), QPoint(100, view.viewport()->height() + 60));
    QTRY_VERIFY(bar->value() > 200);
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, {}, QPoint(100, view.viewport()->height() + 60));
    QVERIFY(view.hasSelection());
    QVERIFY(view.selectedText().contains(QStringLiteral("Section 1")));

    // And past the top edge.
    QTest::mousePress(view.viewport(), Qt::LeftButton, {}, QPoint(100, 200));
    QTest::mouseMove(view.viewport(), QPoint(100, 100));
    QTest::mouseMove(view.viewport(), QPoint(100, -60));
    const int before = bar->value();
    QTRY_VERIFY(bar->value() < before);
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, {}, QPoint(100, -60));

    // Shift+click extends an existing selection.
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::ShiftModifier, QPoint(200, 100));
    QVERIFY(view.hasSelection());
}

void TestViewInput::viewFollowsPaletteAndResize()
{
    DocumentView view;
    view.resize(800, 400);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QByteArray big = longDocument(1500); // slow enough that resizing is debounced
    view.setDocument(parseMarkdown(big));
    QSignalSpy layouts(&view, &DocumentView::layoutChanged);

    view.resize(600, 400);
    QTRY_VERIFY(!layouts.isEmpty());
    const qreal narrow = view.documentLayout()->width();
    view.resize(1000, 300); // height and width both change
    QTRY_VERIFY(view.documentLayout()->width() > narrow);

    QPalette dark = view.palette();
    dark.setColor(QPalette::Base, Qt::black);
    dark.setColor(QPalette::Text, Qt::white);
    layouts.clear();
    view.setPalette(dark);
    QVERIFY(!layouts.isEmpty());
    QVERIFY(view.documentLayout()->theme().dark);

    // Reloading in place keeps the reader's position.
    view.verticalScrollBar()->setValue(3000);
    const int before = view.verticalScrollBar()->value();
    view.setDocument(parseMarkdown(big), true);
    QVERIFY(qAbs(view.verticalScrollBar()->value() - before) < 40);
}

QTEST_MAIN(TestViewInput)
#include "tst_viewinput.moc"
