#include "app/app.h"

#include "app/logging.h"
#include "app/settings.h"
#include "ui/mainwindow.h"

#include <QApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QTimer>
#include <QWindow>

#include <memory>
#include <ranges>

namespace {

constexpr qsizetype MaxMessageBytes = qsizetype(256) * 1024;
constexpr int MaxFilesPerMessage = 64;
constexpr qsizetype MaxPathLength = 4096; // PATH_MAX on Linux
constexpr qsizetype MaxTokenLength = 256;
// A running instance answers at once; waiting longer only delays a launch
// that will open its own window anyway.
constexpr int ConnectTimeoutMs = 300;
constexpr int WriteTimeoutMs = 2000;
constexpr int DisconnectTimeoutMs = 1000;
// A client that connects and then says nothing is dropped, so idle
// connections cannot pile up for the life of the process.
constexpr int IdleConnectionMs = 5000;

QString serverName()
{
    // Lets tests run their own instance without meeting a real one.
    QString override = qEnvironmentVariable("MDGLASS_INSTANCE_NAME");
    if (!override.isEmpty())
        return override;
#ifdef Q_OS_UNIX
    // The per-user runtime directory (mode 0700, owner checked by Qt) keeps
    // other local users from claiming the name first: in the shared /tmp they
    // could listen in our place and receive the paths we hand over. Without
    // one (no session manager, a cron job, sudo) there is no safe place for
    // the socket, so each launch opens its own window instead.
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty())
        return {};
    return QDir(runtime).filePath(QStringLiteral("markdown-glass"));
#else
    return QStringLiteral("markdown-glass-%1").arg(qEnvironmentVariable("USERNAME"));
#endif
}

} // namespace

App::App(QObject *parent)
    : QObject(parent)
{ }

bool App::sendToRunningInstance(const QStringList &files)
{
    const QString name = serverName();
    if (name.isEmpty())
        return false;
    QLocalSocket socket;
    socket.connectToServer(name);
    if (!socket.waitForConnected(ConnectTimeoutMs))
        return false;
    QJsonObject message;
    message.insert(QStringLiteral("files"), QJsonArray::fromStringList(files));
    // Lets the running instance raise its window under Wayland.
    message.insert(QStringLiteral("token"), qEnvironmentVariable("XDG_ACTIVATION_TOKEN"));
    socket.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    if (!socket.waitForBytesWritten(WriteTimeoutMs))
        return false;
    socket.disconnectFromServer();
    if (socket.state() != QLocalSocket::UnconnectedState)
        socket.waitForDisconnected(DisconnectTimeoutMs);
    return true;
}

void App::listen()
{
    const QString name = serverName();
    if (name.isEmpty()) {
        qCInfo(lcInstance) << "No runtime directory: every launch opens its own window";
        return;
    }
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(name)) {
        // A stale socket from a crashed instance; nobody answered when we tried to connect.
        QLocalServer::removeServer(name);
        if (!m_server->listen(name)) {
            // Still usable; later launches just open their own windows.
            qCWarning(lcInstance) << "Single-instance socket unavailable:" << m_server->errorString();
        }
    }
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server->nextPendingConnection()) {
            // The buffer lives in the connections and goes with the socket.
            auto buffer = std::make_shared<QByteArray>();
            auto *idle = new QTimer(socket);
            idle->setSingleShot(true);
            idle->start(IdleConnectionMs);
            connect(idle, &QTimer::timeout, socket, [socket] {
                socket->abort();
                socket->deleteLater();
            });
            connect(socket, &QLocalSocket::readyRead, this, [this, socket, buffer] {
                buffer->append(socket->readAll());
                if (buffer->size() > MaxMessageBytes) {
                    socket->abort();
                    socket->deleteLater();
                    return;
                }
                const qsizetype newline = buffer->indexOf('\n');
                if (newline >= 0) {
                    handleMessage(buffer->left(newline));
                    buffer->clear();
                    socket->disconnectFromServer();
                }
            });
            connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
        }
    });
}

// The socket is user-only, but the message is still treated as untrusted: it
// can only ever name absolute file paths to open.
void App::handleMessage(const QByteArray &message)
{
    const QJsonObject object = QJsonDocument::fromJson(message).object();
    QStringList files;
    const QJsonArray array = object.value(QStringLiteral("files")).toArray();
    for (const auto value : array) {
        const QString path = value.toString();
        if (files.size() < MaxFilesPerMessage && !path.isEmpty() && path.size() < MaxPathLength
            && QDir::isAbsolutePath(path))
            files.append(path);
    }
    const QByteArray token = object.value(QStringLiteral("token")).toString().left(MaxTokenLength).toLatin1();
    // Opening can ask a question, which would run its event loop inside the
    // socket's read handler; the files are opened once the handler is done.
    QTimer::singleShot(0, this, [this, files, token] { open(files, token); });
}

MainWindow *App::createWindow()
{
    auto *window = new MainWindow;
    m_windows.append(window);
    return window;
}

MainWindow *App::activeWindow() const
{
    if (auto *active = qobject_cast<MainWindow *>(QApplication::activeWindow()))
        return active;
    for (const QPointer<MainWindow> &window : std::views::reverse(m_windows)) {
        if (window)
            return window;
    }
    return nullptr;
}

bool App::open(const QStringList &files, const QByteArray &activationToken)
{
    m_windows.removeAll(nullptr);
    const bool tabs = AppSettings::instance().get().openInTabs;

    // QPointer: opening a file can ask a question, and its event loop could
    // let a window, the active one included, be closed and destroyed meanwhile.
    QPointer<MainWindow> target = activeWindow();
    if (files.isEmpty()) {
        target = createWindow();
    } else {
        QList<QPointer<MainWindow>> created;
        for (const QString &file : files) {
            // Reuse the current window when tabs are preferred or it is still empty.
            if (!target || (!tabs && target->hasDocuments())) {
                target = createWindow();
                created.append(target);
            }
            target->openFile(file);
            // Every window that receives a file is shown, not only the last.
            if (target && target->hasDocuments())
                target->show();
        }
        // A window made for a file that did not open (missing, or declined)
        // is not left behind empty.
        for (const QPointer<MainWindow> &window : std::as_const(created)) {
            if (!window || window->hasDocuments())
                continue;
            m_windows.removeAll(window);
            if (window == target)
                target = nullptr;
            window->deleteLater();
        }
        if (!target)
            target = activeWindow();
        if (!target)
            return false;
    }

    if (!activationToken.isEmpty())
        qputenv("XDG_ACTIVATION_TOKEN", activationToken);
    target->show();
    target->raise();
    target->activateWindow();
    if (target->windowHandle())
        target->windowHandle()->requestActivate();
    return true;
}
