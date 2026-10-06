#pragma once

#include <QString>
#include <QStringList>

// Launching an external text editor for the Edit button.

struct EditorLaunch {
    QString program;
    QStringList arguments;
    QString error; // set, with program empty, when nothing suitable was found

    bool isValid() const { return !program.isEmpty(); }
};

// Builds the command line for a user-supplied command such as "code" or
// "kate --new %f". The file replaces a %f (or %F/%u/%U) argument; without
// one it is appended. No shell is involved.
EditorLaunch customEditorLaunch(const QString &command, const QString &file);

// Expands the Exec line of a desktop entry for one local file.
QStringList expandDesktopExec(const QString &exec, const QString &file);

// Reads Exec from a .desktop file. Fails for terminal applications.
EditorLaunch desktopEntryLaunch(const QString &desktopFilePath, const QString &file);

// The desktop's default text editor: the application registered for plain
// text, which is what KDE's "Text editor" default sets.
EditorLaunch systemEditorLaunch(const QString &file);

// Opens the file in the custom command if one is given, else the system default.
bool openInEditor(const QString &file, const QString &customCommand, QString *error);
