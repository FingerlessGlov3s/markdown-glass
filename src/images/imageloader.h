#pragma once

#include "layout/imagesource.h"

#include <QHash>
#include <QHostAddress>
#include <QImage>
#include <QList>
#include <QObject>
#include <QSize>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

// The size an image of `size` pixels is decoded and kept at: scaled down,
// keeping its aspect ratio, to the loader's limits on width and area.
QSize storedImageSize(QSize size);

// Loads and caches the images of one document. Local files are read and
// decoded off the UI thread; remote images are fetched only when allowed.
// All input is untrusted: formats are allow-listed and sizes are capped.
class ImageLoader : public QObject, public md::ImageSource
{
    Q_OBJECT
public:
    explicit ImageLoader(QObject *parent = nullptr);
    ~ImageLoader() override;

    void setBaseDir(const QString &dir);
    void setRemoteAllowed(bool allowed);
    bool remoteAllowed() const { return m_remoteAllowed; }
    // True if at least one requested image was withheld because it is remote.
    bool hasBlocked() const { return m_blocked > 0; }
    // Animated GIF/WebP images play when on, and show their first frame when off.
    void setAnimated(bool animated);
    void clear();
    // Which resolved addresses remote images may be fetched from; the
    // application keeps the default, isPublicAddress(). Tests replace it,
    // since the only server they can run is on this machine.
    using AddressCheck = std::function<bool(const QHostAddress &)>;
    void setAddressCheck(AddressCheck check);

    State request(const QString &src) override;
    QSizeF naturalSize(const QString &src) override;
    void paint(QPainter &painter, const QString &src, const QRectF &target, bool preview) override;

    struct Decoded {
        QImage image;
        QSize natural;
        QByteArray animation; // the encoded file, kept only for multi-frame images
        bool ok = false;
    };
    // Validates and decodes image bytes. Safe to call from any thread.
    static Decoded decode(const QByteArray &data);

signals:
    // One or more images finished loading (coalesced); the layout should be rebuilt.
    void changed();
    void blockedChanged();
    // An animation advanced; a repaint is enough, the layout is unaffected.
    void frameChanged();

private:
    struct Animation;
    struct Entry {
        State state = Pending;
        QImage image;
        QSize natural;
        // Shared because QHash needs Entry to be copyable, and an Animation (a QTimer and a QBuffer) is not.
        std::shared_ptr<Animation> animation;
    };

    void advance(const QString &src);

    void start(const QString &src, Entry &entry);
    // A callable that hands a decoded image back to finish() on the UI thread.
    std::function<void(const Decoded &)> resultDelivery(const QString &src, quint64 generation);
    std::function<void(const Decoded &)> resultDelivery(const QString &src);
    // Remote images wait in a queue so that a document cannot open thousands
    // of connections at once; a download holds its place until its bytes are
    // handed to the decoder or refused.
    void pumpDownloads();
    void downloadDone(quint64 generation);
    void fetchRemote(const QString &src, const QUrl &url, quint64 generation, int redirectsLeft);
    void download(const QString &src, const QUrl &url, const QHostAddress &address, quint64 generation,
                  int redirectsLeft);
    void finish(const QString &src, quint64 generation, const Decoded &decoded);

    struct QueuedDownload {
        QString src;
        QUrl url;
    };

    QHash<QString, Entry> m_entries;
    QString m_baseDir;
    bool m_remoteAllowed = false;
    bool m_animated = true;
    int m_blocked = 0;
    qint64 m_bytes = 0;
    quint64 m_generation = 0; // bumped by clear() so stale results are dropped
    QNetworkAccessManager *m_network = nullptr;
    QList<QueuedDownload> m_queue;
    int m_activeDownloads = 0;
    bool m_pumping = false;
    AddressCheck m_addressCheck;
    QTimer m_notify;
};
