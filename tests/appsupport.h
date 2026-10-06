#pragma once

// Helpers for tests that drive whole windows or the real program.

#include "app/settings.h"
#include "testsupport.h"
#include "ui/mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QProcess>
#include <QUuid>

// The action in `window` whose text (without the & accelerator) is `text`.
inline QAction *findAction(QWidget *window, const QString &text)
{
    const auto actions = window->findChildren<QAction *>();
    for (QAction *a : actions) {
        if (a->text().remove(u'&') == text)
            return a;
    }
    qWarning("no action %s", qPrintable(text));
    return nullptr;
}

inline QList<MainWindow *> visibleMainWindows()
{
    QList<MainWindow *> out;
    const auto widgets = QApplication::topLevelWidgets();
    for (QWidget *w : widgets) {
        if (auto *m = qobject_cast<MainWindow *>(w); m && m->isVisible())
            out.append(m);
    }
    return out;
}

inline void changeSetting(void (*change)(Settings &))
{
    AppSettings::instance().update(change);
}

// Gives this test process its own single-instance socket name, so nothing it
// starts can meet a real Markdown Glass running on the machine.
inline void usePrivateInstanceName()
{
    qputenv("MDGLASS_INSTANCE_NAME", "mdglass-test-" + QUuid::createUuid().toByteArray(QUuid::Id128));
}

// The fixture shared by the suites that open whole windows or the real
// program: a.md links to b.md and the web, b.md links back, and long.md
// scrolls. Also keeps the process away from any Markdown Glass that is running.
inline void setUpWindowSuite(const QString &dir, QString *a, QString *b, QString *longDoc)
{
    *a = writeFile(dir, QStringLiteral("a.md"),
                   "# Alpha\n\nSee [b](b.md) and [web](https://example.invalid).\n\n## Second\n\nText `code`.\n");
    *b = writeFile(dir, QStringLiteral("b.md"), "# Beta\n\nBack to [a](a.md).\n");
    *longDoc = writeFile(dir, QStringLiteral("long.md"), longDocument(200));
    usePrivateInstanceName();
}

// Closes every window and resets settings between tests.
inline void resetApplicationState()
{
    QApplication::closeAllWindows();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    AppSettings::instance().set(Settings());
}

#ifdef MDGLASS_BINARY
// Runs the real markdown-glass binary; returns its exit code, or -1 on timeout.
inline int runProgram(const QStringList &args, QByteArray *output = nullptr, int timeout = 30000)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(QStringLiteral(MDGLASS_BINARY), args);
    if (!p.waitForFinished(timeout)) {
        p.kill();
        p.waitForFinished();
        return -1;
    }
    if (output)
        *output = p.readAll();
    return p.exitCode();
}
#endif
