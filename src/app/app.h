#pragma once

#include <QObject>
#include <QPointer>

class MainWindow;
class QLocalServer;

// Owns the windows and the single-instance channel: a second launch hands its
// file list to the running process, which opens it in a new window or a tab.
class App : public QObject
{
    Q_OBJECT
public:
    explicit App(QObject *parent = nullptr);

    // Returns true if a running instance accepted the files (so this process can exit).
    static bool sendToRunningInstance(const QStringList &files);
    void listen();

    // Opens files in a new window or as tabs in the active one, following the
    // "open in tabs" setting; with no files, opens an empty window. The token
    // lets the window be raised when the request came from another process.
    // Returns false if no window is open afterwards, because every file was
    // missing or the user declined to open it.
    bool open(const QStringList &files, const QByteArray &activationToken = QByteArray());

private:
    MainWindow *createWindow();
    MainWindow *activeWindow() const;
    void handleMessage(const QByteArray &message);

    QLocalServer *m_server = nullptr;
    QList<QPointer<MainWindow>> m_windows;
};
