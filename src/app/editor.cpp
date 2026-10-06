#include "app/editor.h"

#include "app/logging.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

namespace {

constexpr QLatin1StringView ExecKey("Exec=");
constexpr QLatin1StringView TerminalKey("Terminal=");
// xdg-mime is a shell script that reads a few files; longer means something is wrong.
constexpr int QueryTimeoutMs = 3000;

QString tr(const char *text)
{
    return QCoreApplication::translate("Editor", text);
}

EditorLaunch failure(const QString &message)
{
    EditorLaunch launch;
    launch.error = message;
    return launch;
}

// "kde-org.kde.kate.desktop" may live at applications/kde/org.kde.kate.desktop.
QString locateDesktopFile(const QString &id)
{
    QString candidate = id;
    for (;;) {
        QString path = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, candidate);
        if (!path.isEmpty())
            return path;
        const qsizetype dash = candidate.indexOf(u'-');
        if (dash < 0)
            return {};
        candidate[dash] = u'/';
    }
}

} // namespace

QStringList expandDesktopExec(const QString &exec, const QString &file)
{
    QStringList out;
    bool usedFile = false;
    const QStringList parts = QProcess::splitCommand(exec);
    for (const QString &part : parts) {
        if (part == QLatin1String("%f") || part == QLatin1String("%F")) {
            out.append(file);
            usedFile = true;
            continue;
        }
        if (part == QLatin1String("%u") || part == QLatin1String("%U")) {
            out.append(QUrl::fromLocalFile(file).toString());
            usedFile = true;
            continue;
        }
        // The file only ever replaces a whole argument. Inside a longer one,
        // such as the script of `sh -c "ed %f"`, a crafted file name could
        // become code, so an embedded field code is dropped like %i, %c, %k
        // and the deprecated ones, and the file is appended instead.
        QString arg;
        for (qsizetype i = 0; i < part.size(); ++i) {
            if (part[i] != u'%' || i + 1 >= part.size()) {
                arg += part[i];
                continue;
            }
            if (part[++i] == u'%')
                arg += u'%';
        }
        if (!arg.isEmpty())
            out.append(arg);
    }
    if (!usedFile && !out.isEmpty())
        out.append(file);
    return out;
}

EditorLaunch customEditorLaunch(const QString &command, const QString &file)
{
    QStringList parts = expandDesktopExec(command.trimmed(), file);
    if (parts.isEmpty())
        return failure(tr("The editor command is empty."));
    EditorLaunch launch;
    launch.program = parts.takeFirst();
    launch.arguments = parts;
    return launch;
}

EditorLaunch desktopEntryLaunch(const QString &desktopFilePath, const QString &file)
{
    QFile desktop(desktopFilePath);
    if (!desktop.open(QIODevice::ReadOnly))
        return failure(tr("Cannot read %1.").arg(desktopFilePath));

    QString exec;
    bool terminal = false;
    bool inEntry = false;
    while (!desktop.atEnd()) {
        const QString line = QString::fromUtf8(desktop.readLine()).trimmed();
        if (line.startsWith(u'[')) {
            inEntry = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        if (!inEntry)
            continue;
        if (line.startsWith(ExecKey))
            exec = line.mid(ExecKey.size()).replace(QLatin1String("\\\\"), QLatin1String("\\"));
        else if (line.startsWith(TerminalKey))
            terminal = line.mid(TerminalKey.size()).compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
    }
    const QString name = QFileInfo(desktopFilePath).completeBaseName();
    if (exec.isEmpty())
        return failure(tr("%1 has no command to run.").arg(name));
    if (terminal) {
        return failure(tr("The default text editor (%1) runs in a terminal. "
                          "Set an editor command in the settings instead.")
                           .arg(name));
    }
    return customEditorLaunch(exec, file);
}

EditorLaunch systemEditorLaunch(const QString &file)
{
    QProcess query;
    query.start(QStringLiteral("xdg-mime"),
                {QStringLiteral("query"), QStringLiteral("default"), QStringLiteral("text/plain")});
    QString id;
    if (query.waitForFinished(QueryTimeoutMs) && query.exitCode() == 0)
        id = QString::fromUtf8(query.readAllStandardOutput()).trimmed();

    if (!id.isEmpty()) {
        const QString path = locateDesktopFile(id);
        if (!path.isEmpty())
            return desktopEntryLaunch(path, file);
    }

    // No default registered: fall back to a common editor if one is installed.
    for (const char *name : {"kate", "kwrite", "gnome-text-editor", "gedit"}) {
        if (!QStandardPaths::findExecutable(QLatin1String(name)).isEmpty())
            return customEditorLaunch(QLatin1String(name), file);
    }
    return failure(tr("No default text editor is set. Choose one in System Settings, "
                      "or set an editor command in the settings."));
}

bool openInEditor(const QString &file, const QString &customCommand, QString *error)
{
    const EditorLaunch launch =
        customCommand.trimmed().isEmpty() ? systemEditorLaunch(file) : customEditorLaunch(customCommand, file);
    if (!launch.isValid()) {
        if (error)
            *error = launch.error;
        return false;
    }
    qCDebug(lcEditor) << "Launching" << launch.program << launch.arguments;
    if (!QProcess::startDetached(launch.program, launch.arguments, QFileInfo(file).absolutePath())) {
        qCWarning(lcEditor) << "Could not start" << launch.program;
        if (error)
            *error = tr("Could not start \"%1\".").arg(launch.program);
        return false;
    }
    return true;
}
