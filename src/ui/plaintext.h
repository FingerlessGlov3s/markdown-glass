#pragma once

#include <QMessageBox>
#include <QString>

class QWidget;

// Link targets, file names and heading text come from untrusted documents and
// file systems. Qt shows any string that looks like HTML as rich text, and a
// rich-text message box or tooltip follows links and loads <img> sources, so
// such text only reaches the screen through these helpers.

// Gives `box` its informative text and makes all of its text plain. The
// format is set last: in some Qt 6 releases the informative label, created
// on first use, does not take the format the box already has.
void setPlainInformativeText(QMessageBox &box, const QString &informativeText);

void showPlainWarning(QWidget *parent, const QString &title, const QString &text);
QMessageBox::StandardButton askPlainQuestion(QWidget *parent, const QString &title, const QString &text);

// A tooltip that shows `text` literally, whatever it contains.
QString plainToolTip(const QString &text);
