#include "app/editor.h"
#include "app/settings.h"
#include "testsupport.h"

#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

// Settings persistence and parsing editor commands.
class TestConfig : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    void settingsSurviveRestart();
    void editorCommands();
    void systemEditorLookup();
    void openInEditorReportsFailures();

private:
    QTemporaryDir m_dir;
    QString m_a; // a markdown file to open in an editor
};

void TestConfig::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_a = writeFile(m_dir.path(), QStringLiteral("a.md"), "# A\n");
}

// Every setting is flipped from its default, saved, and read back the way a
// fresh start of the application would.
void TestConfig::settingsSurviveRestart()
{
    QCoreApplication::setOrganizationName(QStringLiteral("markdown-glass-test"));
    QCoreApplication::setApplicationName(QStringLiteral("markdown-glass-test"));
    QSettings().clear();

    const Settings defaults;
    Settings changed;
    changed.limitWidth = !defaults.limitWidth;
    changed.widthPixels = 1234;
    changed.wrapCode = !defaults.wrapCode;
    changed.minimap = !defaults.minimap;
    changed.animateImages = !defaults.animateImages;
    changed.outline = !defaults.outline;
    changed.autoLoadRemoteImages = !defaults.autoLoadRemoteImages;
    changed.openInTabs = !defaults.openInTabs;
    changed.linksInNewTab = !defaults.linksInNewTab;
    changed.liveReload = !defaults.liveReload;
    changed.zoom = 1.5;
    changed.editorCommand = QStringLiteral("code --wait %f");
    AppSettings::instance().set(changed);
    QSettings().sync();

    const Settings loaded = AppSettings::load();
    QCOMPARE(loaded.limitWidth, changed.limitWidth);
    QCOMPARE(loaded.widthPixels, changed.widthPixels);
    QCOMPARE(loaded.wrapCode, changed.wrapCode);
    QCOMPARE(loaded.minimap, changed.minimap);
    QCOMPARE(loaded.animateImages, changed.animateImages);
    QCOMPARE(loaded.outline, changed.outline);
    QCOMPARE(loaded.autoLoadRemoteImages, changed.autoLoadRemoteImages);
    QCOMPARE(loaded.openInTabs, changed.openInTabs);
    QCOMPARE(loaded.linksInNewTab, changed.linksInNewTab);
    QCOMPARE(loaded.liveReload, changed.liveReload);
    QCOMPARE(loaded.zoom, changed.zoom);
    QCOMPARE(loaded.editorCommand, changed.editorCommand);
    QSettings().clear();
}

void TestConfig::editorCommands()
{
    const QString file = QStringLiteral("/home/me/My Notes/read me.md");

    EditorLaunch code = customEditorLaunch(QStringLiteral("code"), file);
    QCOMPARE(code.program, QStringLiteral("code"));
    QCOMPARE(code.arguments, QStringList {file});

    EditorLaunch placed = customEditorLaunch(QStringLiteral("  kate --new %f --line 1 "), file);
    QCOMPARE(placed.program, QStringLiteral("kate"));
    QCOMPARE(placed.arguments,
             (QStringList {QStringLiteral("--new"), file, QStringLiteral("--line"), QStringLiteral("1")}));

    QCOMPARE(customEditorLaunch(QStringLiteral("\"/opt/My Editor/run\" -x"), file).program,
             QStringLiteral("/opt/My Editor/run"));
    QVERIFY(!customEditorLaunch(QStringLiteral("   "), file).isValid());

    // The path is always one argument: nothing in it is interpreted.
    const QString nasty = QStringLiteral("/tmp/a; rm -rf ~ $(x) `y` %f.md");
    QCOMPARE(customEditorLaunch(QStringLiteral("code"), nasty).arguments, QStringList {nasty});

    QCOMPARE(expandDesktopExec(QStringLiteral("kate -b %U"), file),
             (QStringList {QStringLiteral("kate"), QStringLiteral("-b"), QUrl::fromLocalFile(file).toString()}));
    QCOMPARE(expandDesktopExec(QStringLiteral("ed --icon %i --name %c 100%% %F"), file),
             (QStringList {QStringLiteral("ed"), QStringLiteral("--icon"), QStringLiteral("--name"),
                           QStringLiteral("100%"), file}));
    // A field code inside a longer argument never puts the file name there.
    QCOMPARE(expandDesktopExec(QStringLiteral("sh -c \"ed %f\" --file=%u"), file),
             (QStringList {QStringLiteral("sh"), QStringLiteral("-c"), QStringLiteral("ed "), QStringLiteral("--file="),
                           file}));

    QTemporaryDir dir;
    const EditorLaunch gui = desktopEntryLaunch(
        writeFile(
            dir.path(), QStringLiteral("gui.desktop"),
            "[Desktop Entry]\nName=Kate\nExec=kate -b %U\nTerminal=false\n\n[Desktop Action new]\nExec=kate --new\n"),
        file);
    QCOMPARE(gui.program, QStringLiteral("kate"));
    QCOMPARE(gui.arguments.size(), 2);

    const EditorLaunch tui = desktopEntryLaunch(
        writeFile(dir.path(), QStringLiteral("vim.desktop"), "[Desktop Entry]\nExec=vim %F\nTerminal=true\n"), file);
    QVERIFY(!tui.isValid());
    QVERIFY(!tui.error.isEmpty());
    QVERIFY(!desktopEntryLaunch(dir.filePath(QStringLiteral("missing.desktop")), file).isValid());
}

void TestConfig::systemEditorLookup()
{
    const QByteArray oldPath = qgetenv("PATH");
    const QByteArray oldData = qgetenv("XDG_DATA_HOME");
    auto restore = qScopeGuard([&] {
        qputenv("PATH", oldPath);
        qputenv("XDG_DATA_HOME", oldData);
    });
    QTemporaryDir bin, data;
    auto script = [&](const QString &name, const QByteArray &body) {
        const QString path = writeFile(bin.path(), name, "#!/bin/sh\n" + body + "\n");
        QFile::setPermissions(path, QFile::permissions(path) | QFile::ExeOwner);
    };
    qputenv("PATH", bin.path().toUtf8());
    qputenv("XDG_DATA_HOME", data.path().toUtf8());
    const QString file = m_a;

    // Nothing registered and no known editor installed.
    EditorLaunch none = systemEditorLaunch(file);
    QVERIFY(!none.isValid());
    QVERIFY(none.error.contains(QStringLiteral("No default text editor")));

    // No registered default, but a common editor is installed.
    script(QStringLiteral("kwrite"), "exit 0");
    QCOMPARE(systemEditorLaunch(file).program, QStringLiteral("kwrite"));

    // The registered default, found by its desktop file id (kde-x -> kde/x).
    QDir(data.path()).mkpath(QStringLiteral("applications/kde"));
    QFile entry(data.filePath(QStringLiteral("applications/kde/fakeedit.desktop")));
    if (!entry.open(QIODevice::WriteOnly))
        qFatal("cannot write test file");
    entry.write("[Desktop Entry]\nType=Application\nName=Fake\nExec=/bin/true --open %U\n");
    entry.close();
    script(QStringLiteral("xdg-mime"), "echo kde-fakeedit.desktop");
    const EditorLaunch launch = systemEditorLaunch(file);
    QCOMPARE(launch.program, QStringLiteral("/bin/true"));
    QCOMPARE(launch.arguments.first(), QStringLiteral("--open"));

    // A terminal editor as the default is refused with an explanation.
    QFile tui(data.filePath(QStringLiteral("applications/vi.desktop")));
    if (!tui.open(QIODevice::WriteOnly))
        qFatal("cannot write test file");
    tui.write("[Desktop Entry]\nExec=vi %F\nTerminal=true\n");
    tui.close();
    script(QStringLiteral("xdg-mime"), "echo vi.desktop");
    QVERIFY(systemEditorLaunch(file).error.contains(QStringLiteral("terminal")));

    // The default is the system editor when no command is configured.
    script(QStringLiteral("xdg-mime"), "echo kde-fakeedit.desktop");
    QString error;
    QVERIFY(openInEditor(file, QString(), &error));
}

void TestConfig::openInEditorReportsFailures()
{
    QString error;
    QVERIFY(openInEditor(m_a, QStringLiteral("/bin/true"), &error));
    QVERIFY(!openInEditor(m_a, QStringLiteral("/nonexistent/editor"), &error));
    QVERIFY(error.contains(QStringLiteral("Could not start")));
    QVERIFY(!desktopEntryLaunch(m_a, m_a).isValid()); // a file with no Exec line
}

QTEST_GUILESS_MAIN(TestConfig)
#include "tst_config.moc"
