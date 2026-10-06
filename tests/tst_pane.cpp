#include "images/imageloader.h"
#include "model/parser.h"
#include "testsupport.h"
#include "ui/documentpane.h"
#include "ui/findbar.h"
#include "view/documentview.h"

#include <QClipboard>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <cstdio>

using namespace md;

// One open document and what surrounds it: loading, live reload, history,
// link handling, the remote-image bar and the find bar.
class TestPane : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();

    void loadErrors();
    void liveReloadKeepsPosition();
    void liveReloadSurvivesReplacedFile();
    void liveReloadCanBeSwitchedOff();
    void history();
    void linkKinds();
    void remoteImagesBar();
    void findBar();
    void binaryFilesAskFirst();
    void declinedBackKeepsTheEntry();
    void reloadsWhenOnlyTheBytesChange();
    void symlinkIsTheSameFile();
    void reloadWaitsWhileAsking();
    void largeFiles();

private:
    QTemporaryDir m_dir;
};

void TestPane::initTestCase()
{
    QVERIFY(m_dir.isValid());
}

void TestPane::cleanup()
{
    QApplication::closeAllWindows();
}

void TestPane::loadErrors()
{
    DocumentPane pane;
    QCOMPARE(pane.title(), QStringLiteral("Untitled"));
    const LoadResult missing = pane.load(m_dir.filePath(QStringLiteral("missing.md")));
    QVERIFY(!missing);
    QVERIFY(missing.failed());
    QVERIFY(missing.message.contains(QStringLiteral("not a file")));
    QVERIFY(pane.load(m_dir.path()).failed());
    pane.reload(); // nothing loaded: a no-op

    // A UTF-8 byte order mark is ignored.
    const QString bom = writeFile(m_dir.path(), QStringLiteral("bom.md"), "\xEF\xBB\xBF# Heading\n");
    QVERIFY(pane.load(bom));
    QCOMPARE(pane.document()->blocks.front()->type, Block::Heading);
    QCOMPARE(pane.title(), QStringLiteral("bom.md"));
}

void TestPane::liveReloadKeepsPosition()
{
    const QString path = writeFile(m_dir.path(), QStringLiteral("live.md"), longDocument(80));
    DocumentPane pane;
    pane.resize(800, 600);
    pane.show();
    QVERIFY(pane.load(path));
    QScrollBar *bar = pane.view()->verticalScrollBar();
    bar->setValue(bar->maximum() / 2);
    const int before = bar->value();

    QSignalSpy changed(&pane, &DocumentPane::documentChanged);
    writeFile(m_dir.path(), QStringLiteral("live.md"), longDocument(80, "## Added at the end\n\nNew text.\n"));
    QTRY_COMPARE_WITH_TIMEOUT(changed.size(), 1, 10000);
    QCOMPARE(pane.document()->outline.last().title, QStringLiteral("Added at the end"));
    QVERIFY(qAbs(bar->value() - before) < 40);
}

void TestPane::liveReloadSurvivesReplacedFile()
{
    // Many editors save by writing a new file and renaming it over the old one.
    const QString path = writeFile(m_dir.path(), QStringLiteral("atomic.md"), "# Version one\n");
    DocumentPane pane;
    QVERIFY(pane.load(path));
    QSignalSpy changed(&pane, &DocumentPane::documentChanged);

    for (int version = 2; version <= 3; ++version) {
        const QString temp = writeFile(m_dir.path(), QStringLiteral("atomic.md.tmp"),
                                       "# Version " + QByteArray::number(version) + " is here\n");
        QVERIFY(std::rename(QFile::encodeName(temp).constData(), QFile::encodeName(path).constData()) == 0);
        QTRY_COMPARE_WITH_TIMEOUT(changed.size(), version - 1, 10000);
        QCOMPARE(pane.document()->outline.first().title, QStringLiteral("Version %1 is here").arg(version));
    }

    // Deleting the file keeps the last good copy on screen.
    QFile::remove(path);
    QTest::qWait(400); // a fixed wait: this checks that nothing happens

    QCOMPARE(pane.document()->outline.first().title, QStringLiteral("Version 3 is here"));
}

void TestPane::liveReloadCanBeSwitchedOff()
{
    const QString path = writeFile(m_dir.path(), QStringLiteral("off.md"), "# Before\n");
    DocumentPane pane;
    Settings s;
    s.liveReload = false;
    pane.applySettings(s);
    QVERIFY(pane.load(path));
    QSignalSpy changed(&pane, &DocumentPane::documentChanged);
    writeFile(m_dir.path(), QStringLiteral("off.md"), "# After the change\n");
    QTest::qWait(600); // a fixed wait: this checks that nothing happens
    QCOMPARE(changed.size(), 0);
    pane.reload(); // the Reload action still works
    QCOMPARE(pane.document()->outline.first().title, QStringLiteral("After the change"));
}

void TestPane::history()
{
    const QString a = writeFile(m_dir.path(), QStringLiteral("h-a.md"), longDocument(60));
    const QString b = writeFile(m_dir.path(), QStringLiteral("h-b.md"), "# B\n");
    DocumentPane pane;
    pane.resize(800, 600);
    pane.show();
    QVERIFY(pane.load(a));
    QScrollBar *bar = pane.view()->verticalScrollBar();
    bar->setValue(500);

    QSignalSpy history(&pane, &DocumentPane::historyChanged);
    QVERIFY(pane.load(b));
    QVERIFY(pane.canGoBack());
    QVERIFY(!pane.canGoForward());

    pane.goBack();
    QCOMPARE(pane.path(), a);
    QCOMPARE(bar->value(), 500); // where we left it
    QVERIFY(pane.canGoForward());
    pane.goForward();
    QCOMPARE(pane.path(), b);
    QVERIFY(history.size() >= 3);

    // A file that vanished from the history is reported, not opened.
    QFile::remove(a);
    QSignalSpy status(&pane, &DocumentPane::statusMessage);
    pane.goBack();
    QCOMPARE(pane.path(), b);
    QVERIFY(!status.isEmpty());
    pane.goForward(); // nothing to go forward to
}

void TestPane::linkKinds()
{
    writeFile(m_dir.path(), QStringLiteral("script.sh"), "echo hi\n");
    const QString doc = writeFile(m_dir.path(), QStringLiteral("links.md"), longDocument(30));
    DocumentPane pane;
    pane.resize(800, 600);
    pane.show();
    QVERIFY(pane.load(doc));
    DocumentView *view = pane.view();
    QSignalSpy status(&pane, &DocumentPane::statusMessage);

    // Anchors in this document.
    emit view->linkActivated(QStringLiteral("#section-20"), OpenIn::CurrentTab);
    QVERIFY(view->verticalScrollBar()->value() > 0);
    emit view->linkActivated(QStringLiteral("#no-such-heading"), OpenIn::CurrentTab);
    QVERIFY(status.last().at(0).toString().contains(QStringLiteral("no-such-heading")));
    // A link to this same file with an anchor just scrolls.
    emit view->linkActivated(QStringLiteral("links.md#top"), OpenIn::CurrentTab);
    QVERIFY(view->verticalScrollBar()->value() < 50); // the first heading, with a small margin
    QCOMPARE(pane.path(), doc);

    // A local file that is not markdown is never launched: offered as a folder instead.
    {
        ModalCloser closer;
        emit view->linkActivated(QStringLiteral("script.sh"), OpenIn::CurrentTab);
        QCOMPARE(closer.titles.value(0), QStringLiteral("Local File Link"));
    }
    // A missing file.
    {
        ModalCloser closer;
        emit view->linkActivated(QStringLiteral("nowhere.md"), OpenIn::CurrentTab);
        QVERIFY(closer.texts.value(0).contains(QStringLiteral("does not exist")));
    }
    // An unsupported scheme, with the link copied from the dialog.
    {
        ModalCloser closer(QStringLiteral("Copy Link"));
        emit view->linkActivated(QStringLiteral("javascript:alert(1)"), OpenIn::CurrentTab);
        QCOMPARE(closer.titles.value(0), QStringLiteral("Unsupported Link"));
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("javascript:alert(1)"));
    }
    // Link text that looks like HTML is shown literally: a rich-text dialog
    // would offer the embedded link and load the image.
    const QString markup = QStringLiteral("x:<a href=\"https://evil.test\">click</a><img src=\"/dev/zero\">");
    {
        ModalCloser closer;
        emit view->linkActivated(markup, OpenIn::CurrentTab);
        QCOMPARE(closer.titles.value(0), QStringLiteral("Unsupported Link"));
        QCOMPARE(closer.details.value(0), markup);
        QCOMPARE(closer.formats.value(0), Qt::PlainText);
        QCOMPARE(closer.detailFormats.value(0), Qt::PlainText);
    }
    {
        ModalCloser closer;
        emit view->linkActivated(QStringLiteral("<b>bold</b>.txt"), OpenIn::CurrentTab);
        QCOMPARE(closer.titles.value(0), QStringLiteral("Local File Link"));
        QVERIFY(closer.details.value(0).endsWith(QStringLiteral("<b>bold</b>.txt")));
        QCOMPARE(closer.formats.value(0), Qt::PlainText);
        QCOMPARE(closer.detailFormats.value(0), Qt::PlainText);
    }
    QCOMPARE(pane.path(), doc);
}

void TestPane::remoteImagesBar()
{
    const QString doc =
        writeFile(m_dir.path(), QStringLiteral("remote.md"), "# Remote\n\n![x](https://127.0.0.1/x.png)\n");
    DocumentPane pane;
    pane.resize(800, 600);
    pane.show();
    QVERIFY(pane.load(doc));
    QPushButton *load = nullptr;
    const auto buttons = pane.findChildren<QPushButton *>();
    for (QPushButton *b : buttons) {
        if (b->text() == QStringLiteral("Load Remote Images"))
            load = b;
    }
    QVERIFY(load);
    QTRY_VERIFY(load->parentWidget()->isVisible());
    load->click();
    QTRY_VERIFY(!load->parentWidget()->isVisible());

    // Reloading the same file keeps the permission; another file starts blocked again.
    pane.reload();
    QVERIFY(!load->parentWidget()->isVisible());
    const QString other = writeFile(m_dir.path(), QStringLiteral("remote2.md"), "![y](https://127.0.0.1/y.png)\n");
    QVERIFY(pane.load(other));
    QTRY_VERIFY(load->parentWidget()->isVisible());

    // With automatic loading on, the bar never appears.
    Settings s;
    s.autoLoadRemoteImages = true;
    pane.applySettings(s);
    QVERIFY(pane.load(doc));
    QTest::qWait(100); // a fixed wait: this checks that the bar stays hidden
    QVERIFY(!load->parentWidget()->isVisible());
}

void TestPane::findBar()
{
    const QString doc =
        writeFile(m_dir.path(), QStringLiteral("find.md"), "Needle one. Another needle. NEEDLE three.\n");
    DocumentPane pane;
    pane.resize(800, 600);
    pane.show();
    QVERIFY(pane.load(doc));
    auto *bar = pane.findChild<FindBar *>();
    auto *edit = bar->findChild<QLineEdit *>();
    QLabel *count = bar->findChildren<QLabel *>().last();

    pane.findNext(FindDirection::Forward); // with the bar hidden, this opens it
    QVERIFY(bar->isVisible());
    edit->setText(QStringLiteral("needle"));
    QCOMPARE(count->text(), QStringLiteral("1 of 3"));
    QTest::keyClick(edit, Qt::Key_Return);
    QCOMPARE(count->text(), QStringLiteral("2 of 3"));
    QTest::keyClick(edit, Qt::Key_Return, Qt::ShiftModifier);
    QCOMPARE(count->text(), QStringLiteral("1 of 3"));

    // The match-case button narrows the matches.
    QToolButton *caseButton = nullptr;
    const auto tools = bar->findChildren<QToolButton *>();
    for (QToolButton *t : tools) {
        if (t->isCheckable())
            caseButton = t;
    }
    caseButton->click();
    QVERIFY(bar->caseSensitive());
    QCOMPARE(count->text(), QStringLiteral("1 of 1"));
    edit->setText(QStringLiteral("absent"));
    QCOMPARE(count->text(), QStringLiteral("No matches"));
    edit->clear();
    QVERIFY(count->text().isEmpty());

    // The previous and next arrow buttons, then Escape closes the bar.
    edit->setText(QStringLiteral("e"));
    for (QToolButton *t : tools) {
        if (!t->isCheckable() && t->toolTip().contains(QStringLiteral("match")))
            t->click();
    }
    QTest::keyClick(edit, Qt::Key_Escape);
    QVERIFY(!bar->isVisible());
    QCOMPARE(pane.view()->matchCount(), 0);

    // Selected text seeds the search.
    pane.view()->selectAll();
    pane.showFind();
    QVERIFY(edit->text().isEmpty() || !edit->text().contains(u'\n'));
}

void TestPane::binaryFilesAskFirst()
{
    const QString text = writeFile(m_dir.path(), QStringLiteral("good.md"), "# Good\n");
    const QString binary =
        writeFile(m_dir.path(), QStringLiteral("model.stl"), QByteArray("STLB\0\0\x01\0", 8).repeated(64));
    DocumentPane pane;
    QVERIFY(pane.load(text));

    // Declined (the default): nothing changes, and no error is reported.
    {
        ModalCloser closer;
        const LoadResult result = pane.load(binary);
        QVERIFY(!result);
        QCOMPARE(result.status, LoadResult::Declined);
        QVERIFY(result.message.isEmpty());
        QCOMPARE(closer.titles.value(0), QStringLiteral("Not a Text File"));
        QVERIFY(closer.texts.value(0).contains(QStringLiteral("model.stl")));
        QCOMPARE(closer.formats.value(0), Qt::PlainText);
        QCOMPARE(pane.path(), text);
    }
    // "Open Anyway" shows it.
    {
        ModalCloser closer(QStringLiteral("Open Anyway"));
        QVERIFY(pane.load(binary));
        QCOMPARE(pane.path(), binary);
    }
    // A text file that turns binary is not reloaded; the last good copy stays.
    QVERIFY(pane.load(text));
    QSignalSpy status(&pane, &DocumentPane::statusMessage);
    writeFile(m_dir.path(), QStringLiteral("good.md"), QByteArray("\0\0\0binary now", 13));
    pane.reload();
    QVERIFY(!status.isEmpty());
    QVERIFY(status.last().at(0).toString().contains(QStringLiteral("no longer looks like a text file")));
    QCOMPARE(pane.document()->outline.first().title, QStringLiteral("Good"));
}

void TestPane::declinedBackKeepsTheEntry()
{
    const QString a = writeFile(m_dir.path(), QStringLiteral("back-a.md"), "# A\n");
    const QString b = writeFile(m_dir.path(), QStringLiteral("back-b.md"), "# B\n");
    DocumentPane pane;
    QVERIFY(pane.load(a));
    QVERIFY(pane.load(b));
    QVERIFY(pane.canGoBack());

    // The file behind the history entry turned binary; declining to open it
    // leaves the entry where it was, so Back can be tried again.
    writeFile(m_dir.path(), QStringLiteral("back-a.md"), QByteArray("\0\0\0\0binary", 10));
    {
        ModalCloser closer;
        pane.goBack();
        QCOMPARE(closer.titles.value(0), QStringLiteral("Not a Text File"));
    }
    QCOMPARE(pane.path(), b);
    QVERIFY(pane.canGoBack());
    QVERIFY(!pane.canGoForward());

    // Once it is text again, Back works and the entry moves to Forward.
    writeFile(m_dir.path(), QStringLiteral("back-a.md"), "# A again\n");
    pane.goBack();
    QCOMPARE(pane.path(), a);
    QVERIFY(!pane.canGoBack());
    QVERIFY(pane.canGoForward());
}

void TestPane::reloadsWhenOnlyTheBytesChange()
{
    const QString path = writeFile(m_dir.path(), QStringLiteral("same.md"), "# One\n");
    DocumentPane pane;
    QVERIFY(pane.load(path));
    const QDateTime modified = QFileInfo(path).lastModified();
    QSignalSpy changed(&pane, &DocumentPane::documentChanged);

    // Same size, and the modification time put back: only the bytes differ.
    writeFile(m_dir.path(), QStringLiteral("same.md"), "# Two\n");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(modified, QFileDevice::FileModificationTime));
    }
    QCOMPARE(QFileInfo(path).lastModified(), modified);
    QCOMPARE(QFileInfo(path).size(), qint64(6));
    QTRY_COMPARE_WITH_TIMEOUT(changed.size(), 1, 10000);
    QCOMPARE(pane.document()->outline.first().title, QStringLiteral("Two"));
}

void TestPane::symlinkIsTheSameFile()
{
    const QString real =
        writeFile(m_dir.path(), QStringLiteral("real.md"), "# Real\n\n![x](https://127.0.0.1/x.png)\n");
    const QString link = m_dir.filePath(QStringLiteral("link.md"));
    QFile::remove(link);
    QVERIFY(QFile::link(real, link));
    const QString other = writeFile(m_dir.path(), QStringLiteral("other.md"), "# Other\n");

    DocumentPane pane;
    QVERIFY(pane.load(real));
    pane.images()->setRemoteAllowed(true);
    // Opening the link is opening the same file: no history entry, and the
    // permission for remote images is kept, while the name shown is the link's.
    QVERIFY(pane.load(link));
    QCOMPARE(pane.path(), link);
    QCOMPARE(pane.canonicalPath(), DocumentPane::canonicalise(real));
    QCOMPARE(pane.title(), QStringLiteral("link.md"));
    QVERIFY(!pane.canGoBack());
    QVERIFY(pane.images()->remoteAllowed());

    QVERIFY(pane.load(other));
    QVERIFY(pane.canGoBack());
    QVERIFY(!pane.images()->remoteAllowed());

    // A link to this same file with an anchor just scrolls, link or not.
    QVERIFY(pane.load(link));
    QSignalSpy changed(&pane, &DocumentPane::documentChanged);
    emit pane.view()->linkActivated(QStringLiteral("real.md#real"), OpenIn::CurrentTab);
    QCOMPARE(changed.size(), 0);
    QCOMPARE(pane.path(), link);

    // A missing file has no canonical path; its absolute path stands in.
    const QString missing = m_dir.filePath(QStringLiteral("missing.md"));
    QCOMPARE(DocumentPane::canonicalise(missing), QFileInfo(missing).absoluteFilePath());
}

void TestPane::reloadWaitsWhileAsking()
{
    const QString text = writeFile(m_dir.path(), QStringLiteral("asking.md"), "# Asking\n");
    const QString binary = writeFile(m_dir.path(), QStringLiteral("asking.bin"), QByteArray("\0\0\0\0bin", 7));
    DocumentPane pane;
    QVERIFY(pane.load(text));
    QSignalSpy changed(&pane, &DocumentPane::documentChanged);

    // While the "Not a Text File" question is up, the file changes and a
    // reload is asked for. It must not read on top of the question; it waits.
    int reloadsWhileAsking = 0;
    int opensWhileAsking = 0;
    QTimer::singleShot(60, &pane, [&] {
        if (QApplication::activeModalWidget()) {
            ++reloadsWhileAsking;
            writeFile(m_dir.path(), QStringLiteral("asking.md"), "# Asking again\n");
            pane.reload();
            // Opening another file meanwhile is refused outright.
            const LoadResult result = pane.load(text);
            QVERIFY(result.failed());
            QVERIFY(result.message.contains(QStringLiteral("still being opened")));
            ++opensWhileAsking;
        }
    });
    QTimer::singleShot(120, &pane, [&] {
        if (QWidget *modal = QApplication::activeModalWidget())
            modal->close(); // declines
    });
    QCOMPARE(pane.load(binary).status, LoadResult::Declined);
    QCOMPARE(reloadsWhileAsking, 1);
    QCOMPARE(opensWhileAsking, 1);
    QCOMPARE(pane.path(), text);
    QCOMPARE(pane.document()->outline.first().title, QStringLiteral("Asking")); // not read during the question
    // The deferred reload runs once the question is answered.
    QTRY_COMPARE_WITH_TIMEOUT(changed.size(), 1, 5000);
    QCOMPARE(pane.document()->outline.first().title, QStringLiteral("Asking again"));
}

void TestPane::largeFiles()
{
    constexpr qint64 Megabyte = 1024 * 1024;
    // Sparse files: the size is what matters, and nothing is written.
    const QString large = m_dir.filePath(QStringLiteral("large.md"));
    const QString huge = m_dir.filePath(QStringLiteral("huge.md"));
    {
        QFile f(large);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QVERIFY(f.resize(21 * Megabyte));
        QFile g(huge);
        QVERIFY(g.open(QIODevice::WriteOnly));
        QVERIFY(g.resize(201 * Megabyte));
    }
    DocumentPane pane;

    // Over the warning size: asked first; declining opens nothing.
    {
        ModalCloser closer;
        QCOMPARE(pane.load(large).status, LoadResult::Declined);
        QCOMPARE(closer.titles, QStringList {QStringLiteral("Large File")});
        QVERIFY(closer.texts.value(0).contains(QStringLiteral("21 MB")));
    }
    QVERIFY(pane.path().isEmpty());
    // Accepting reads it; the zero bytes then fail the text check, which is
    // declined as well, so both questions are seen in turn.
    {
        ModalCloser closer(QStringLiteral("Yes"));
        QCOMPARE(pane.load(large).status, LoadResult::Declined);
        QCOMPARE(closer.titles, (QStringList {QStringLiteral("Large File"), QStringLiteral("Not a Text File")}));
    }
    // Over the hard limit: refused without a question.
    {
        ModalCloser closer;
        const LoadResult result = pane.load(huge);
        QVERIFY(result.failed());
        QVERIFY(result.message.contains(QStringLiteral("too large")));
        QVERIFY(result.message.contains(QStringLiteral("201 MB")));
        QVERIFY(closer.titles.isEmpty());
    }
    QFile::remove(large);
    QFile::remove(huge);
}

QTEST_MAIN(TestPane)
#include "tst_pane.moc"
