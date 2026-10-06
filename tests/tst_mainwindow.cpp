#include "appsupport.h"
#include "testsupport.h"
#include "ui/documentpane.h"
#include "ui/mainwindow.h"
#include "ui/outlinedock.h"
#include "view/documentview.h"

#include <QClipboard>
#include <QDropEvent>
#include <QMimeData>
#include <QPointer>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>

// The main window: tabs, menus and toolbar actions, dialogs, printing and
// drag and drop.
class TestMainWindow : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();

    void openingFilesAndTabs();
    void symlinkOpensTheExistingTab();
    void viewActionsChangeSettings();
    void navigationAndEditing();
    void dialogsAndPrinting();
    void dragAndDrop();
    void linksOpenInNewTabs();
    void closingTheLastTab();

private:
    QTemporaryDir m_dir;
    QString m_a, m_b, m_long;
};

void TestMainWindow::initTestCase()
{
    QVERIFY(m_dir.isValid());
    setUpWindowSuite(m_dir.path(), &m_a, &m_b, &m_long);
}

void TestMainWindow::cleanup()
{
    resetApplicationState();
}

void TestMainWindow::openingFilesAndTabs()
{
    auto *window = new MainWindow;
    window->show();
    QVERIFY(!window->hasDocuments());
    auto *tabs = window->findChild<QTabWidget *>();

    DocumentPane *a = window->openFile(m_a);
    QVERIFY(a);
    QCOMPARE(window->windowTitle(), QStringLiteral("a.md"));
    QVERIFY(window->hasDocuments());

    // Opening the same file again switches to its tab rather than duplicating it.
    DocumentPane *b = window->openFile(m_b);
    QCOMPARE(tabs->count(), 2);
    QCOMPARE(window->openFile(m_a, QStringLiteral("second")), a);
    QCOMPARE(tabs->count(), 2);
    QCOMPARE(tabs->currentWidget(), a);

    // Replacing the current tab's document.
    tabs->setCurrentWidget(b);
    QCOMPARE(window->openFile(m_long, QString(), OpenIn::CurrentTab), b);
    QCOMPARE(b->path(), m_long);
    QCOMPARE(tabs->count(), 2);

    // Failures are reported, not fatal.
    ModalCloser closer;
    QVERIFY(!window->openFile(m_dir.filePath(QStringLiteral("missing.md"))));
    QVERIFY(!window->openFile(m_dir.filePath(QStringLiteral("missing.md")), QString(), OpenIn::CurrentTab));
    QTRY_COMPARE(closer.closed, 2);
    QCOMPARE(tabs->count(), 2);

    // The outline follows the current tab.
    auto *outline = window->findChild<OutlineDock *>();
    QVERIFY(outline);
    tabs->setCurrentWidget(a);
    emit a->view()->linkHovered(QStringLiteral("https://example.invalid"));
    QCOMPARE(window->statusBar()->currentMessage(), QStringLiteral("https://example.invalid"));
    emit a->view()->linkHovered(QString());
    QVERIFY(window->statusBar()->currentMessage().isEmpty());
}

void TestMainWindow::symlinkOpensTheExistingTab()
{
    const QString link = m_dir.filePath(QStringLiteral("a-link.md"));
    QFile::remove(link);
    QVERIFY(QFile::link(m_a, link));
    auto *window = new MainWindow;
    window->show();
    auto *tabs = window->findChild<QTabWidget *>();

    DocumentPane *a = window->openFile(m_a);
    QVERIFY(a);
    // The link names the file already open: its tab is shown, not a second copy.
    QCOMPARE(window->openFile(link, QStringLiteral("second")), a);
    QCOMPARE(tabs->count(), 1);
    QCOMPARE(a->path(), m_a);
}

void TestMainWindow::viewActionsChangeSettings()
{
    auto *window = new MainWindow;
    window->show();
    DocumentPane *pane = window->openFile(m_long);

    const Settings before = AppSettings::instance().get();
    findAction(window, QStringLiteral("Limit Text Width"))->trigger();
    QCOMPARE(AppSettings::instance().get().limitWidth, !before.limitWidth);
    findAction(window, QStringLiteral("Wrap Code Blocks"))->trigger();
    QCOMPARE(AppSettings::instance().get().wrapCode, !before.wrapCode);
    findAction(window, QStringLiteral("Document Preview on Scrollbar"))->trigger();
    QCOMPARE(AppSettings::instance().get().minimap, !before.minimap);
    findAction(window, QStringLiteral("Outline Sidebar"))->trigger();
    QCOMPARE(AppSettings::instance().get().outline, !before.outline);
    QCOMPARE(window->findChild<OutlineDock *>()->isVisible(), !before.outline);

    findAction(window, QStringLiteral("Zoom In"))->trigger();
    QVERIFY(AppSettings::instance().get().zoom > 1.0);
    QVERIFY(pane->view()->zoom() > 1.0);
    findAction(window, QStringLiteral("Zoom Out"))->trigger();
    findAction(window, QStringLiteral("Zoom Out"))->trigger();
    QVERIFY(AppSettings::instance().get().zoom < 1.0);
    findAction(window, QStringLiteral("Actual Size"))->trigger();
    QCOMPARE(AppSettings::instance().get().zoom, 1.0);

    // Ctrl+wheel zoom in a view becomes the shared zoom.
    pane->view()->setZoom(1.5);
    QCOMPARE(AppSettings::instance().get().zoom, 1.5);

    QAction *fullScreen = findAction(window, QStringLiteral("Full Screen"));
    fullScreen->trigger();
    fullScreen->trigger();
}

void TestMainWindow::navigationAndEditing()
{
    auto *window = new MainWindow;
    window->show();
    DocumentPane *pane = window->openFile(m_a);
    QAction *back = findAction(window, QStringLiteral("Back"));
    QAction *forward = findAction(window, QStringLiteral("Forward"));
    QVERIFY(!back->isEnabled());

    // Following a link to another markdown file replaces the document.
    emit pane->view()->linkActivated(QStringLiteral("b.md"), OpenIn::CurrentTab);
    QCOMPARE(pane->path(), m_b);
    QVERIFY(back->isEnabled());
    back->trigger();
    QCOMPARE(pane->path(), m_a);
    QVERIFY(forward->isEnabled());
    forward->trigger();
    QCOMPARE(pane->path(), m_b);

    findAction(window, QStringLiteral("Reload"))->trigger();
    findAction(window, QStringLiteral("Select All"))->trigger();
    QVERIFY(pane->view()->hasSelection());
    findAction(window, QStringLiteral("Copy"))->trigger();
    QCOMPARE(QApplication::clipboard()->text(), pane->view()->selectedText());

    findAction(window, QStringLiteral("Find..."))->trigger();
    findAction(window, QStringLiteral("Find Next"))->trigger();
    findAction(window, QStringLiteral("Find Previous"))->trigger();

    // Edit runs the configured editor; a broken command is reported.
    changeSetting([](Settings &s) { s.editorCommand = QStringLiteral("/bin/true"); });
    findAction(window, QStringLiteral("Edit in Text Editor"))->trigger();
    changeSetting([](Settings &s) { s.editorCommand = QStringLiteral("/nonexistent/editor"); });
    ModalCloser closer;
    findAction(window, QStringLiteral("Edit in Text Editor"))->trigger();
    QCOMPARE(closer.titles.value(0), QStringLiteral("Cannot Open Editor"));
}

void TestMainWindow::dialogsAndPrinting()
{
    auto *window = new MainWindow;
    window->show();
    window->openFile(m_a);

    ModalCloser closer;
    findAction(window, QStringLiteral("Configure Markdown Glass..."))->trigger();
    findAction(window, QStringLiteral("About Markdown Glass"))->trigger();
    findAction(window, QStringLiteral("Print Preview..."))->trigger();
    findAction(window, QStringLiteral("Print..."))->trigger();
    findAction(window, QStringLiteral("Open..."))->trigger();
    QCOMPARE(closer.closed, 5);
    QVERIFY(closer.titles.contains(QStringLiteral("About Markdown Glass")));
    QVERIFY(closer.titles.contains(QStringLiteral("Print Preview - a.md")));
}

void TestMainWindow::dragAndDrop()
{
    auto *window = new MainWindow;
    window->show();
    QMimeData mime;
    mime.setUrls(
        {QUrl::fromLocalFile(m_a), QUrl::fromLocalFile(m_b), QUrl(QStringLiteral("https://example.invalid/x.md"))});

    QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(window, &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(window, &drop);
    QCOMPARE(window->findChild<QTabWidget *>()->count(), 2);
}

void TestMainWindow::linksOpenInNewTabs()
{
    auto *window = new MainWindow;
    window->show();
    DocumentPane *pane = window->openFile(m_a);
    emit pane->view()->linkActivated(QStringLiteral("b.md#beta"), OpenIn::NewTab);
    auto *tabs = window->findChild<QTabWidget *>();
    QCOMPARE(tabs->count(), 2);
    QCOMPARE(qobject_cast<DocumentPane *>(tabs->currentWidget())->path(), m_b);
}

void TestMainWindow::closingTheLastTab()
{
    QPointer<MainWindow> window = new MainWindow;
    window->show();
    window->openFile(m_a);
    window->openFile(m_b);
    QAction *close = findAction(window, QStringLiteral("Close Tab"));
    close->trigger();
    close->trigger();
    QVERIFY(!window->hasDocuments());
    QCOMPARE(window->windowTitle(), QStringLiteral("Markdown Glass"));
    // With nothing left to close, Close Tab closes the window.
    close->trigger();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(window.isNull());
}

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"
