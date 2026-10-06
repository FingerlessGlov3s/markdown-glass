#pragma once

#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QMessageBox>
#include <QStringList>
#include <QTimer>

// Closes modal dialogs and popup menus as they appear, so code that calls
// exec() can be driven from a test. Optionally presses a message box button
// by its text (without the & accelerator).
class ModalCloser : public QObject
{
public:
    explicit ModalCloser(const QString &button = QString())
        : m_button(button)
    {
        connect(&m_timer, &QTimer::timeout, this, &ModalCloser::tick);
        m_timer.start(30);
    }

    int closed = 0;
    QStringList titles;
    QStringList texts;
    QStringList details;                 // informative text
    QList<Qt::TextFormat> formats;       // how each box interprets its text
    QList<Qt::TextFormat> detailFormats; // ...and its informative text

private:
    void tick()
    {
        if (QWidget *popup = QApplication::activePopupWidget()) {
            ++closed;
            popup->close();
            return;
        }
        QWidget *modal = QApplication::activeModalWidget();
        if (!modal)
            return;
        ++closed;
        titles << modal->windowTitle();
        if (auto *box = qobject_cast<QMessageBox *>(modal)) {
            texts << box->text();
            details << box->informativeText();
            formats << box->textFormat();
            const auto *detail = box->findChild<QLabel *>(QStringLiteral("qt_msgbox_informativelabel"));
            detailFormats << (detail ? detail->textFormat() : Qt::AutoText); // not found: fails a PlainText check
            if (!m_button.isEmpty()) {
                const auto buttons = box->buttons();
                for (QAbstractButton *button : buttons) {
                    if (button->text().remove(u'&') == m_button) {
                        button->click();
                        return;
                    }
                }
            }
        }
        if (auto *dialog = qobject_cast<QDialog *>(modal))
            dialog->reject();
        else
            modal->close();
    }

    QTimer m_timer;
    QString m_button;
};

// Writes a file for a test, replacing any existing one, and returns its path.
inline QString writeFile(const QString &dir, const QString &name, const QByteArray &content)
{
    const QString path = QDir(dir).filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        qFatal("cannot write test file %s", qPrintable(path));
    f.write(content);
    return path;
}

// A document with a title and `sections` numbered sections, long enough to scroll.
inline QByteArray longDocument(int sections, const QByteArray &tail = QByteArray())
{
    QByteArray out = "# Top\n\n";
    for (int i = 0; i < sections; ++i)
        out += "## Section " + QByteArray::number(i) + "\n\nSome paragraph text in section " + QByteArray::number(i)
            + ".\n\n";
    return out + tail;
}
