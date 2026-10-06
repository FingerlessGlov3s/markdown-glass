#include "ui/plaintext.h"

#include <QTextDocument>

void setPlainInformativeText(QMessageBox &box, const QString &informativeText)
{
    box.setInformativeText(informativeText);
    box.setTextFormat(Qt::PlainText);
}

void showPlainWarning(QWidget *parent, const QString &title, const QString &text)
{
    QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::Ok, parent);
    box.setTextFormat(Qt::PlainText);
    box.exec();
}

QMessageBox::StandardButton askPlainQuestion(QWidget *parent, const QString &title, const QString &text)
{
    QMessageBox box(QMessageBox::Question, title, text, QMessageBox::Yes | QMessageBox::No, parent);
    box.setTextFormat(Qt::PlainText);
    return static_cast<QMessageBox::StandardButton>(box.exec());
}

QString plainToolTip(const QString &text)
{
    // Tooltips have no plain-text mode, so the text is escaped into rich text.
    return text.isEmpty() ? text : Qt::convertFromPlainText(text, Qt::WhiteSpaceNoWrap);
}
