#pragma once

#include "app/settings.h"
#include "view/documentview.h"

#include <QDateTime>
#include <QTimer>
#include <QWidget>

#include <memory>

class FindBar;
class ImageLoader;
class Minimap;
class QFileSystemWatcher;
class QFrame;

namespace md {
struct Document;
enum class NotText;
} // namespace md

// How opening a file ended. Declining (a very large file, or one that does
// not look like text) is the user's choice and is never reported as an error.
struct LoadResult {
    enum Status { Loaded, Declined, Failed };
    Status status = Loaded;
    QString message; // set when Failed

    explicit operator bool() const { return status == Loaded; }
    bool failed() const { return status == Failed; }
};

// One open document: the view, its minimap, find bar, remote-image bar,
// navigation history and file watching. Lives in a tab.
class DocumentPane : public QWidget
{
    Q_OBJECT
public:
    explicit DocumentPane(QWidget *parent = nullptr);
    ~DocumentPane() override;

    // Loads a markdown file, replacing the current one and recording history.
    LoadResult load(const QString &path, const QString &anchor = QString());
    void reload();

    // The path as it was opened, for display.
    QString path() const { return m_path; }
    // The file behind it, with symbolic links resolved, for telling whether
    // two paths are the same file; see canonicalise().
    QString canonicalPath() const { return m_canonicalPath; }
    // The canonical form of any path, or its absolute form when the file does
    // not exist (a missing file has no canonical path).
    static QString canonicalise(const QString &path);
    QString title() const;
    DocumentView *view() const { return m_view; }
    ImageLoader *images() const { return m_images; }
    const md::Document *document() const;

    void applySettings(const Settings &settings);
    void showFind();
    void findNext(FindDirection direction);

    bool canGoBack() const { return !m_back.isEmpty(); }
    bool canGoForward() const { return !m_forward.isEmpty(); }
    void goBack();
    void goForward();

signals:
    void titleChanged();
    void documentChanged();
    void historyChanged();
    void statusMessage(const QString &message);
    void openInNewTab(const QString &path, const QString &anchor);

private:
    struct HistoryEntry {
        QString path;
        int scroll = 0;
    };

    // Reads and shows a file without touching the history. keepPosition is
    // true for reloads of the same file.
    LoadResult read(const QString &path, bool keepPosition);
    void navigate(QList<HistoryEntry> &from, QList<HistoryEntry> &to);
    bool confirmOpenBinary(const QString &fileName, md::NotText reason);
    void activateLink(const QString &href, OpenIn where);
    void watch();
    void fileEvent();
    bool contentChanged() const;
    void runFind();
    void updateRemoteBar();

    DocumentView *m_view;
    Minimap *m_minimap;
    FindBar *m_findBar;
    QFrame *m_remoteBar;
    ImageLoader *m_images;
    QFileSystemWatcher *m_watcher;
    QTimer m_reloadTimer;

    QString m_path;
    QString m_canonicalPath;
    QDateTime m_modified;
    qint64 m_size = -1;
    QByteArray m_contentHash;   // of the bytes last read, for changes that keep size and time
    bool m_reading = false;     // read() is up, possibly inside one of its dialogs
    bool m_fileChanged = false; // the file itself, not just its directory, changed since the last check
    bool m_liveReload = true;
    bool m_autoLoadRemote = false;
    bool m_linksInNewTab = false;
    QList<HistoryEntry> m_back;
    QList<HistoryEntry> m_forward;
};
