#include "app/app.h"
#include "appsupport.h"
#include "testsupport.h"
#include "ui/documentpane.h"

#include <QLocalSocket>
#include <QProcess>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>

// Opening files from outside: the single-instance socket, handing files to a
// running copy, and choosing between new windows and tabs.
class TestInstance : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();

    void singleInstanceHandoff();
    void singleInstanceRejectsBadMessages();
    void idleConnectionsAreDropped();
    void filesOpenInNewWindowsOrTabs();
    void secondLaunchHandsOver();
    void declinedFilesLeaveNoEmptyWindow();

private:
    QTemporaryDir m_dir;
    QString m_a, m_b, m_long;
};

void TestInstance::initTestCase()
{
    QVERIFY(m_dir.isValid());
    setUpWindowSuite(m_dir.path(), &m_a, &m_b, &m_long);
}

void TestInstance::cleanup()
{
    resetApplicationState();
}

void TestInstance::singleInstanceHandoff()
{
    App app;
    app.listen();
    QVERIFY(App::sendToRunningInstance({m_a}));
    QTRY_COMPARE(visibleMainWindows().size(), 1);
    QCOMPARE(visibleMainWindows().first()->findChild<DocumentPane *>()->path(), m_a);
}

void TestInstance::singleInstanceRejectsBadMessages()
{
    App app;
    app.listen();
    const QString name = qEnvironmentVariable("MDGLASS_INSTANCE_NAME");

    // Oversized: dropped without opening anything.
    {
        QLocalSocket socket;
        socket.connectToServer(name);
        QVERIFY(socket.waitForConnected(1000));
        socket.write(QByteArray(300 * 1024, 'x'));
        socket.waitForBytesWritten(1000);
        QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
    }
    QCOMPARE(visibleMainWindows().size(), 0);

    // Not JSON, and relative or junk paths: nothing is opened from them, only an empty window.
    {
        QLocalSocket socket;
        socket.connectToServer(name);
        QVERIFY(socket.waitForConnected(1000));
        socket.write("{\"files\": [\"relative.md\", 42, \"\"], \"token\": \"t\"}\n");
        socket.waitForBytesWritten(1000);
        QTRY_COMPARE(visibleMainWindows().size(), 1);
        QVERIFY(!visibleMainWindows().first()->hasDocuments());
    }
}

void TestInstance::idleConnectionsAreDropped()
{
    App app;
    app.listen();
    const QString name = qEnvironmentVariable("MDGLASS_INSTANCE_NAME");

    // Connects and says nothing: dropped by the server after its idle time
    // (5 s), so silent clients cannot hold connections for ever.
    QLocalSocket idle;
    idle.connectToServer(name);
    QVERIFY(idle.waitForConnected(1000));
    QTest::qWait(3000); // a fixed wait: still connected well within the idle time
    QCOMPARE(idle.state(), QLocalSocket::ConnectedState);
    QTRY_COMPARE_WITH_TIMEOUT(idle.state(), QLocalSocket::UnconnectedState, 7000);
    QCOMPARE(visibleMainWindows().size(), 0);

    // A message on a later connection still opens its file, after the
    // handler has returned to the event loop.
    QLocalSocket talker;
    talker.connectToServer(name);
    QVERIFY(talker.waitForConnected(1000));
    talker.write("{\"files\": [\"" + m_a.toUtf8() + "\"], \"token\": \"t\"}\n");
    talker.waitForBytesWritten(1000);
    QTRY_COMPARE(visibleMainWindows().size(), 1);
    QCOMPARE(visibleMainWindows().first()->findChild<DocumentPane *>()->path(), m_a);
    QTRY_COMPARE(talker.state(), QLocalSocket::UnconnectedState);
}

void TestInstance::filesOpenInNewWindowsOrTabs()
{
    App app;
    changeSetting([](Settings &s) { s.openInTabs = false; });
    app.open({m_a, m_b});
    QCOMPARE(visibleMainWindows().size(), 2);
    QApplication::closeAllWindows();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    changeSetting([](Settings &s) { s.openInTabs = true; });
    app.open({m_a, m_b});
    QCOMPARE(visibleMainWindows().size(), 1);
    QCOMPARE(visibleMainWindows().first()->findChild<QTabWidget *>()->count(), 2);
    app.open({m_long}, "token");
    QCOMPARE(visibleMainWindows().size(), 1);
    QCOMPARE(visibleMainWindows().first()->findChild<QTabWidget *>()->count(), 3);
}

void TestInstance::secondLaunchHandsOver()
{
    QProcess first;
    first.start(QStringLiteral(MDGLASS_BINARY), {});
    const QString name = qEnvironmentVariable("MDGLASS_INSTANCE_NAME");
    QTRY_VERIFY_WITH_TIMEOUT(
        [&] {
            QLocalSocket probe;
            probe.connectToServer(name);
            return probe.waitForConnected(100);
        }(),
        15000);

    // The second launch passes its file on and exits straight away.
    QCOMPARE(runProgram({m_a}, nullptr, 15000), 0);
    first.terminate();
    if (!first.waitForFinished(5000))
        first.kill();
}

void TestInstance::declinedFilesLeaveNoEmptyWindow()
{
    const QString binary = writeFile(m_dir.path(), QStringLiteral("photo.png"), QByteArray("\x89PNG\0\0\0\0", 8));
    App app;
    {
        ModalCloser closer; // declines the "not a text file" question
        QVERIFY(!app.open({binary}));
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(visibleMainWindows().size(), 0);

    // With one good file among them, only that window remains.
    {
        ModalCloser closer;
        QVERIFY(app.open({binary, m_a}));
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(visibleMainWindows().size(), 1);
    QVERIFY(visibleMainWindows().first()->hasDocuments());
}

QTEST_MAIN(TestInstance)
#include "tst_instance.moc"
