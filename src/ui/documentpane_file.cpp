#include "ui/documentpane.h"

// DocumentPane: reading files, the checks before showing one, and reloading
// when it changes on disk.

#include "app/logging.h"
#include "images/imageloader.h"
#include "model/parser.h"
#include "model/regularfile.h"
#include "model/textcheck.h"
#include "ui/plaintext.h"
#include "view/documentview.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QMessageBox>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScrollBar>

#include <utility>

namespace {
// Guards against accidentally opening something enormous.
constexpr qint64 Megabyte = qint64(1024) * 1024;
constexpr qint64 WarnDocumentBytes = 20 * Megabyte;
constexpr qint64 MaxDocumentBytes = 200 * Megabyte;
// Only has to tell two versions of one file apart, so the fastest will do.
constexpr QCryptographicHash::Algorithm ContentHash = QCryptographicHash::Md5;

QByteArray hashOf(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, ContentHash);
}
} // namespace

QString DocumentPane::canonicalise(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

// Binary files (images, PDFs, 3D models...) shown as text are pages of
// gibberish that are slow to lay out, so the user is asked first. "Open
// Anyway" stays available for text in an unusual encoding.
bool DocumentPane::confirmOpenBinary(const QString &fileName, md::NotText reason)
{
    QString why;
    switch (reason) {
    case md::NotText::NulBytes:
        why = tr("It contains binary data, as images, PDFs and other non-text files do.");
        break;
    case md::NotText::ControlCharacters:
        why = tr("It contains many control characters that do not appear in text.");
        break;
    case md::NotText::InvalidUtf8:
        why = tr("Much of it is not valid UTF-8 text.");
        break;
    case md::NotText::No:
        return true;
    }
    QMessageBox box(QMessageBox::Question, tr("Not a Text File"),
                    tr("\"%1\" does not look like a markdown or text file.").arg(fileName), QMessageBox::NoButton,
                    window());
    setPlainInformativeText(box, why + u' ' + tr("Opening it would show unreadable characters."));
    QPushButton *open = box.addButton(tr("Open Anyway"), QMessageBox::AcceptRole);
    box.setDefaultButton(box.addButton(QMessageBox::Cancel));
    box.exec();
    return box.clickedButton() == open;
}

LoadResult DocumentPane::read(const QString &path, bool keepPosition)
{
    auto fail = [](const QString &message) { return LoadResult {LoadResult::Failed, message}; };
    // The questions below run nested event loops, in which a socket message
    // could ask this pane to open another file; it is refused while this one
    // is still being decided (reloads wait instead, see reload()).
    if (m_reading)
        return fail(tr("%1 is still being opened.").arg(m_path));
    const QScopedValueRollback<bool> reading(m_reading, true);

    const QFileInfo info(path);
    if (!info.isFile())
        return fail(tr("%1 is not a file.").arg(path));
    if (info.size() > MaxDocumentBytes)
        return fail(tr("%1 is too large to open (%2 MB).").arg(path).arg(info.size() / Megabyte));
    if (info.size() > WarnDocumentBytes && !keepPosition) {
        const auto answer = askPlainQuestion(
            window(), tr("Large File"),
            tr("%1 is %2 MB. Opening it may take a while. Continue?").arg(info.fileName()).arg(info.size() / Megabyte));
        if (answer != QMessageBox::Yes)
            return {LoadResult::Declined, QString()};
    }
    QFile file(path);
    QString reason;
    if (!md::openRegularFile(file, &reason))
        return fail(tr("Cannot open %1: %2").arg(path, reason));
    QByteArray markdown = file.read(MaxDocumentBytes);
    if (const md::NotText notText = md::checkText(markdown); notText != md::NotText::No) {
        // A reload keeps showing the last good copy rather than gibberish.
        if (keepPosition)
            return fail(tr("%1 no longer looks like a text file.").arg(info.fileName()));
        if (!confirmOpenBinary(info.fileName(), notText))
            return {LoadResult::Declined, QString()};
    }
    m_contentHash = hashOf(markdown);
    if (markdown.startsWith(md::Utf8Bom))
        markdown.remove(0, md::Utf8Bom.size());

    const QString canonical = canonicalise(path);
    const bool sameFile = canonical == m_canonicalPath;
    m_path = info.absoluteFilePath();
    m_canonicalPath = canonical;
    m_modified = info.lastModified();
    m_size = info.size();

    // A different file starts with remote images blocked again; a reload of
    // the same file keeps whatever the user already allowed.
    const bool allowed = m_autoLoadRemote || (sameFile && m_images->remoteAllowed());
    m_images->clear();
    m_images->setRemoteAllowed(allowed);
    m_images->setBaseDir(info.absolutePath());

    m_view->setDocument(md::parseMarkdown(markdown), keepPosition);
    updateRemoteBar();
    watch();
    emit documentChanged();
    emit titleChanged();
    return {};
}

LoadResult DocumentPane::load(const QString &path, const QString &anchor)
{
    const QString previous = m_path;
    const QString previousCanonical = m_canonicalPath;
    const int scroll = m_view->verticalScrollBar()->value();
    const LoadResult result = read(QFileInfo(path).absoluteFilePath(), false);
    if (!result)
        return result;
    // Opening the same file by another name (a symbolic link) is not a move
    // worth going back from.
    if (!previous.isEmpty() && previousCanonical != m_canonicalPath) {
        m_back.append(HistoryEntry {previous, scroll});
        m_forward.clear();
    }
    if (!anchor.isEmpty())
        m_view->scrollToAnchor(anchor);
    emit historyChanged();
    return result;
}

void DocumentPane::reload()
{
    if (m_path.isEmpty())
        return;
    if (m_reading) {
        // A file event arrived while read() is asking the user something:
        // try again once that is settled rather than reading on top of it.
        m_reloadTimer.start();
        return;
    }
    const LoadResult result = read(m_path, true);
    if (result.failed())
        emit statusMessage(result.message);
}

void DocumentPane::watch()
{
    const QStringList files = m_watcher->files();
    if (!files.isEmpty())
        m_watcher->removePaths(files);
    const QStringList dirs = m_watcher->directories();
    if (!dirs.isEmpty())
        m_watcher->removePaths(dirs);
    if (m_path.isEmpty())
        return;
    // Editors often save by replacing the file, which drops a file watch, so
    // the directory is watched as well and the file watch is re-added on reload.
    m_watcher->addPath(m_path);
    m_watcher->addPath(QFileInfo(m_path).absolutePath());
}

// Whether the file's bytes differ from those last read. Only consulted when
// size and modification time match, which a quick successive save (or a tool
// that restores the time) can leave unchanged.
bool DocumentPane::contentChanged() const
{
    QFile file(m_path);
    if (!md::openRegularFile(file))
        return false;
    return hashOf(file.read(MaxDocumentBytes)) != m_contentHash;
}

void DocumentPane::fileEvent()
{
    if (m_path.isEmpty())
        return;
    const QFileInfo info(m_path);
    if (!info.isFile())
        return; // mid-save or deleted; keep showing what we have
    if (!m_watcher->files().contains(m_path))
        m_watcher->addPath(m_path);
    const bool fileChanged = std::exchange(m_fileChanged, false);
    if (!m_liveReload)
        return;
    // A write that keeps the size and the time (within a millisecond) shows
    // only in the bytes. Reading them again is worth it when the file itself
    // reported a change, not for every event in its directory.
    if (info.lastModified() == m_modified && info.size() == m_size && !(fileChanged && contentChanged()))
        return;
    qCDebug(lcFiles) << "Reloading changed file" << m_path;
    reload();
}
