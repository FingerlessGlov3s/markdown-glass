#include "model/parser.h"
#include "testsupport.h"
#include "ui/outlinedock.h"
#include "view/documentview.h"
#include "view/minimap.h"

#include <QHBoxLayout>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
#include <QTreeWidget>

using namespace md;

Q_DECLARE_METATYPE(const md::Block *)

// Getting around a document: the outline sidebar and the minimap.
class TestNavigation : public QObject
{
    Q_OBJECT
private slots:
    void cleanup();

    void outlineTreeAndTracking();
    void outlineDragDoesNotMoveHighlight();
    void minimapScrollsTheView();
};

void TestNavigation::cleanup()
{
    QApplication::closeAllWindows();
}

void TestNavigation::outlineTreeAndTracking()
{
    const auto doc = parseMarkdown("# One\n\n## One.A\n\n### One.A.i\n\n## One.B\n\n# Two\n\n## Two.A\n");
    OutlineDock dock;
    dock.show();
    dock.setDocument(doc.get());
    auto *tree = dock.findChild<QTreeWidget *>();
    QCOMPARE(tree->topLevelItemCount(), 2);
    QCOMPARE(tree->topLevelItem(0)->childCount(), 2);
    QCOMPARE(tree->topLevelItem(0)->child(0)->child(0)->text(0), QStringLiteral("One.A.i"));

    const Block *two = doc->outline.at(4).block;
    dock.setCurrent(two);
    QCOMPARE(tree->currentItem()->text(0), QStringLiteral("Two"));
    dock.setCurrent(two); // unchanged

    QSignalSpy activated(&dock, &OutlineDock::headingActivated);
    QTreeWidgetItem *item = tree->topLevelItem(0)->child(1);
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, {}, tree->visualItemRect(item).center());
    QCOMPARE(activated.size(), 1);
    QCOMPARE(activated[0].at(0).value<const Block *>(), doc->outline.at(3).block);
    emit tree->itemActivated(item, 0); // keyboard Enter
    QCOMPARE(activated.size(), 2);

    dock.setDocument(nullptr);
    QCOMPARE(tree->topLevelItemCount(), 0);

    // A heading whose text is markup shows it literally in the tooltip,
    // rather than as an image Qt would go and load.
    const auto hostile = parseMarkdown("# &lt;img src=\"/dev/zero\"&gt;\n");
    dock.setDocument(hostile.get());
    const QString tip = tree->topLevelItem(0)->toolTip(0);
    QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("<img src=\"/dev/zero\">"));
    QVERIFY(!tip.contains(QStringLiteral("<img")));
    QVERIFY(tip.contains(QStringLiteral("&lt;img")));
}

void TestNavigation::outlineDragDoesNotMoveHighlight()
{
    const auto doc = parseMarkdown("# One\n\n# Two\n\n# Three\n\n# Four\n");
    OutlineDock dock;
    dock.resize(250, 300);
    dock.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dock));
    dock.setDocument(doc.get());
    auto *tree = dock.findChild<QTreeWidget *>();
    dock.setCurrent(doc->outline.at(0).block);

    QSignalSpy activated(&dock, &OutlineDock::headingActivated);
    const QPoint second = tree->visualItemRect(tree->topLevelItem(1)).center();
    const QPoint fourth = tree->visualItemRect(tree->topLevelItem(3)).center();
    QTest::mousePress(tree->viewport(), Qt::LeftButton, {}, second);
    QTest::mouseMove(tree->viewport(), fourth);
    // Released elsewhere: no navigation, and the highlight returns to where the document is.
    QTest::mouseRelease(tree->viewport(), Qt::LeftButton, {}, fourth);
    QCOMPARE(activated.size(), 0);
    QTRY_COMPARE(tree->currentItem(), tree->topLevelItem(0));
}

void TestNavigation::minimapScrollsTheView()
{
    QWidget host;
    auto *layout = new QHBoxLayout(&host);
    auto *view = new DocumentView(&host);
    auto *map = new Minimap(view, &host);
    layout->addWidget(view, 1);
    layout->addWidget(map);
    host.resize(900, 500);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    QCOMPARE(map->sizeHint().width(), 112);

    // Nothing to show yet.
    map->grab();
    QTest::mouseClick(map, Qt::LeftButton, {}, QPoint(50, 50));

    view->setDocument(parseMarkdown(longDocument(400)));
    QScrollBar *bar = view->verticalScrollBar();
    QVERIFY(bar->maximum() > 0);
    QVERIFY(!map->grab().isNull());

    // Clicking low in the strip jumps there.
    QTest::mouseClick(map, Qt::LeftButton, {}, QPoint(50, map->height() - 20));
    QVERIFY(bar->value() > bar->maximum() / 2);

    // Dragging the box back to the top scrolls back up.
    QTest::mouseClick(map, Qt::LeftButton, {}, QPoint(50, 5));
    const int top = bar->value();
    QTest::mousePress(map, Qt::LeftButton, {}, QPoint(50, 5));
    QTest::mouseMove(map, QPoint(50, map->height() / 2));
    QVERIFY(bar->value() > top);
    QTest::mouseMove(map, QPoint(50, 0));
    QTest::mouseRelease(map, Qt::LeftButton, {}, QPoint(50, 0));
    QCOMPARE(bar->value(), 0);
    QTest::mouseClick(map, Qt::RightButton, {}, QPoint(50, 50)); // ignored

    // The wheel scrolls the document too.
    QWheelEvent wheel(QPointF(50, 50), map->mapToGlobal(QPointF(50, 50)), QPoint(), QPoint(0, -240), Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(map, &wheel);
    QVERIFY(bar->value() > 0);

    QEnterEvent enter(QPointF(5, 5), QPointF(5, 5), map->mapToGlobal(QPointF(5, 5)));
    QApplication::sendEvent(map, &enter);
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(map, &leave);
    map->grab();
}

QTEST_MAIN(TestNavigation)
#include "tst_navigation.moc"
