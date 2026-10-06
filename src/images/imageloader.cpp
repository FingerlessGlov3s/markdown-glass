#include "images/imageloader.h"

#include "app/logging.h"
#include "images/publicaddress.h"
#include "images/svgcheck.h"
#include "model/document.h"
#include "model/regularfile.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostInfo>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QPainter>
#include <QPointer>
#include <QRegularExpression>
#include <QThreadPool>

#include <algorithm>
#include <cmath>

namespace {

constexpr qint64 MaxImageFileBytes = qint64(40) * 1024 * 1024;
constexpr qint64 MaxDownloadBytes = qint64(20) * 1024 * 1024;
constexpr qint64 MaxSourcePixels = 100'000'000; // refuse decompression bombs
constexpr int MaxStoredWidth = 2400;            // larger images are decoded scaled down
constexpr qint64 MaxStoredPixels = 16'000'000;  // so are tall, narrow ones (64 MB as ARGB)
constexpr qint64 MaxCacheBytes = qint64(160) * 1024 * 1024;
constexpr int DownloadTimeoutMs = 20'000;
constexpr int MaxRedirects = 5;
constexpr int MaxConcurrentDownloads = 20;    // enough to fill a page quickly, few enough to stay polite
constexpr int DecoderAllocationLimitMb = 256; // QImageReader refuses images needing more
constexpr int NotifyCoalesceMs = 60;          // image arrivals within this window cause one relayout
constexpr int FallbackFrameDelayMs = 100;
constexpr qint64 MaxAnimationBytes = qint64(16) * 1024 * 1024;
constexpr qint64 MaxAnimationPixels = 4'000'000; // per frame
constexpr int MinFrameDelayMs = 20;
constexpr int MaxFrameDelayMs = 10'000;
constexpr int IdleAfterMs = 1500; // animations nobody is looking at stop advancing

// Never stores or sends cookies.
class NoCookies : public QNetworkCookieJar
{
public:
    using QNetworkCookieJar::QNetworkCookieJar;
    QList<QNetworkCookie> cookiesForUrl(const QUrl &) const override { return {}; }
    bool setCookiesFromUrl(const QList<QNetworkCookie> &, const QUrl &) override { return false; }
};

bool allowedFormat(const QByteArray &format)
{
    static const QList<QByteArray> allowed = {"png", "jpeg", "jpg", "gif", "webp", "svg"};
    return allowed.contains(format.toLower());
}

} // namespace

// Frames are pulled from the decoder one at a time, so an animation costs its
// encoded bytes plus a single frame, and frame delays can be clamped.
struct ImageLoader::Animation {
    QByteArray data;
    QBuffer buffer;
    QImageReader reader;
    QTimer timer;
    QSize scaledSize;
    QElapsedTimer lastPaint;
    int loopsLeft = -1; // -1: forever
    bool finished = false;

    void rewind()
    {
        buffer.close();
        buffer.setData(data);
        buffer.open(QIODevice::ReadOnly);
        reader.setDevice(&buffer);
        reader.setDecideFormatFromContent(true);
        reader.setAllocationLimit(DecoderAllocationLimitMb);
        if (scaledSize.isValid())
            reader.setScaledSize(scaledSize);
    }
};

QSize storedImageSize(QSize size)
{
    qreal scale = 1;
    if (size.width() > MaxStoredWidth)
        scale = qreal(MaxStoredWidth) / size.width();
    const qreal pixels = qreal(size.width()) * size.height() * scale * scale;
    if (pixels > MaxStoredPixels)
        scale *= std::sqrt(MaxStoredPixels / pixels);
    if (scale >= 1)
        return size;
    // A very narrow image keeps a width of one pixel, which would let its
    // height alone exceed the area limit, so the height is capped as well.
    const int width = qMax(1, int(size.width() * scale));
    const int height = qMax(1, int(qMin<qreal>(size.height() * scale, qreal(MaxStoredPixels) / width)));
    return QSize(width, height);
}

ImageLoader::ImageLoader(QObject *parent)
    : QObject(parent)
    , m_addressCheck(isPublicAddress)
{
    m_notify.setSingleShot(true);
    m_notify.setInterval(NotifyCoalesceMs);
    connect(&m_notify, &QTimer::timeout, this, &ImageLoader::changed);
}

ImageLoader::~ImageLoader() = default;

void ImageLoader::setAddressCheck(AddressCheck check)
{
    m_addressCheck = check ? std::move(check) : AddressCheck(isPublicAddress);
}

void ImageLoader::setBaseDir(const QString &dir)
{
    m_baseDir = dir;
}

void ImageLoader::clear()
{
    ++m_generation;
    m_entries.clear();
    m_bytes = 0;
    // Downloads still running belong to the old generation: their results
    // are dropped, and they no longer count against the new document.
    m_queue.clear();
    m_activeDownloads = 0;
    if (m_blocked != 0) {
        m_blocked = 0;
        emit blockedChanged();
    }
}

void ImageLoader::setAnimated(bool animated)
{
    if (m_animated == animated)
        return;
    m_animated = animated;
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        Animation *a = it->animation.get();
        if (!a)
            continue;
        a->timer.stop();
        // Start over from the first frame, which is also what "off" displays.
        a->rewind();
        a->finished = false;
        a->loopsLeft = a->reader.loopCount();
        advance(it.key());
    }
    emit frameChanged();
}

void ImageLoader::advance(const QString &src)
{
    const auto it = m_entries.find(src);
    if (it == m_entries.end() || !it->animation)
        return;
    Animation &a = *it->animation;
    if (!a.reader.canRead()) {
        if (a.loopsLeft == 0) {
            a.finished = true;
            return;
        }
        if (a.loopsLeft > 0)
            --a.loopsLeft;
        a.rewind();
    }
    const QImage frame = a.reader.read();
    if (frame.isNull()) {
        a.finished = true; // damaged file: keep the last good frame
        return;
    }
    it->image = frame;
    emit frameChanged();

    if (!m_animated)
        return;
    int delay = a.reader.nextImageDelay();
    if (delay < MinFrameDelayMs)
        delay = FallbackFrameDelayMs; // what browsers do with missing or absurd delays
    // Only keep going while something is painting this image.
    if (a.lastPaint.isValid() && a.lastPaint.elapsed() < IdleAfterMs)
        a.timer.start(qMin(delay, MaxFrameDelayMs));
}

void ImageLoader::setRemoteAllowed(bool allowed)
{
    if (m_remoteAllowed == allowed)
        return;
    m_remoteAllowed = allowed;
    if (allowed && m_blocked > 0) {
        // Forget the blocked entries so the next layout pass requests them again.
        for (auto it = m_entries.begin(); it != m_entries.end();) {
            if (it->state == Blocked)
                it = m_entries.erase(it);
            else
                ++it;
        }
        m_blocked = 0;
        emit blockedChanged();
        emit changed();
    }
}

ImageLoader::Decoded ImageLoader::decode(const QByteArray &data)
{
    Decoded out;
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);

    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    reader.setAllocationLimit(DecoderAllocationLimitMb);
    const QByteArray format = reader.format();
    if (!reader.canRead() || !allowedFormat(format))
        return out;
    const bool svg = format.toLower() == "svg";
    if (svg && !svgIsSelfContained(data))
        return out;

    const QSize size = reader.size();
    if (!size.isValid() || size.isEmpty() || qint64(size.width()) * size.height() > MaxSourcePixels)
        return out;
    out.natural = size;

    QSize target = size;
    if (svg) {
        // Vector art is rasterised at twice its nominal size to stay crisp when scaled.
        target = size * 2;
    }
    target = storedImageSize(target);
    if (target != size)
        reader.setScaledSize(target);

    out.image = reader.read();
    out.ok = !out.image.isNull();
    if (out.ok && !svg && reader.supportsAnimation() && reader.imageCount() > 1 && data.size() <= MaxAnimationBytes
        && qint64(size.width()) * size.height() <= MaxAnimationPixels)
        out.animation = data;
    return out;
}

md::ImageSource::State ImageLoader::request(const QString &src)
{
    auto it = m_entries.find(src);
    if (it == m_entries.end()) {
        it = m_entries.insert(src, Entry());
        start(src, *it);
    }
    return it->state;
}

QSizeF ImageLoader::naturalSize(const QString &src)
{
    return m_entries.value(src).natural;
}

void ImageLoader::paint(QPainter &painter, const QString &src, const QRectF &target, bool preview)
{
    const auto it = m_entries.constFind(src);
    if (it == m_entries.constEnd() || it->image.isNull())
        return;
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(target, it->image);
    painter.restore();

    // Only a real view counts as the image being looked at; a minimap tile
    // would otherwise keep an animation ticking for a document scrolled away.
    if (preview)
        return;
    if (Animation *a = it->animation.get()) {
        a->lastPaint.restart();
        if (m_animated && !a->finished && !a->timer.isActive())
            a->timer.start(MinFrameDelayMs); // was idle: resume
    }
}

std::function<void(const ImageLoader::Decoded &)> ImageLoader::resultDelivery(const QString &src, quint64 generation)
{
    // Called on a worker thread. Hops back to the UI thread via the application
    // object, which outlives the loader; a loader deleted meanwhile is skipped,
    // and a result for a cleared generation is dropped in finish().
    return [self = QPointer<ImageLoader>(this), src, generation](const Decoded &decoded) {
        QMetaObject::invokeMethod(
            qApp,
            [self, src, generation, decoded] {
                if (self)
                    self->finish(src, generation, decoded);
            },
            Qt::QueuedConnection);
    };
}

std::function<void(const ImageLoader::Decoded &)> ImageLoader::resultDelivery(const QString &src)
{
    return resultDelivery(src, m_generation);
}

void ImageLoader::start(const QString &src, Entry &entry)
{
    auto deliver = resultDelivery(src);

    if (src.startsWith(QLatin1String("data:"), Qt::CaseInsensitive)) {
        const qsizetype comma = src.indexOf(u',');
        if (comma < 0 || !src.left(comma).endsWith(QLatin1String(";base64"), Qt::CaseInsensitive)
            || src.size() > MaxDownloadBytes) {
            entry.state = Failed;
            return;
        }
        const QByteArray data = QByteArray::fromBase64(QStringView(src).mid(comma + 1).toLatin1());
        QThreadPool::globalInstance()->start([data, deliver] { deliver(decode(data)); });
        return;
    }

    if (md::isRemoteUrl(src)) {
        if (!m_remoteAllowed) {
            entry.state = Blocked;
            if (++m_blocked == 1)
                emit blockedChanged();
            return;
        }
        const QUrl url(src.startsWith(QLatin1String("//")) ? QStringLiteral("https:") + src : src);
        if (!url.isValid() || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))) {
            entry.state = Failed;
            return;
        }
        // A literal address is refused here; host names once resolved.
        if (const QHostAddress literal(url.host()); !literal.isNull() && !m_addressCheck(literal)) {
            entry.state = Failed;
            return;
        }
        m_queue.append(QueuedDownload {src, url});
        pumpDownloads();
        return;
    }

    // Local file: a path relative to the document, an absolute path or a file: URL.
    QString path;
    const QUrl url(src);
    if (url.isValid() && !url.scheme().isEmpty() && url.scheme().size() > 1) {
        if (!url.isLocalFile()) {
            entry.state = Failed; // unsupported scheme
            return;
        }
        path = url.toLocalFile();
    } else {
        QString decoded = QUrl::fromPercentEncoding(src.toUtf8());
        const qsizetype cut = decoded.indexOf(QRegularExpression(QStringLiteral("[?#]")));
        if (cut >= 0)
            decoded.truncate(cut);
        path = QDir(m_baseDir).absoluteFilePath(decoded);
    }

    QThreadPool::globalInstance()->start([path, deliver] {
        Decoded decoded;
        // Regular files only: never devices, FIFOs or directories.
        QFile file(path);
        if (md::openRegularFile(file) && file.size() > 0 && file.size() <= MaxImageFileBytes)
            decoded = decode(file.read(MaxImageFileBytes));
        deliver(decoded);
    });
}

void ImageLoader::pumpDownloads()
{
    // A download refused on the spot calls back into here; the loop below
    // picks up the freed place itself.
    if (m_pumping)
        return;
    m_pumping = true;
    while (m_activeDownloads < MaxConcurrentDownloads && !m_queue.isEmpty()) {
        const QueuedDownload next = m_queue.takeFirst();
        ++m_activeDownloads;
        fetchRemote(next.src, next.url, m_generation, MaxRedirects);
    }
    m_pumping = false;
}

void ImageLoader::downloadDone(quint64 generation)
{
    if (generation != m_generation)
        return; // already forgotten by clear()
    --m_activeDownloads;
    pumpDownloads();
}

// Remote images may only come from public addresses, so a document cannot
// make the viewer probe the local machine or network (routers, admin pages).
// The host is resolved and checked before each request, and every redirect
// goes through the same check. The connection then goes to the address that
// was checked, not to the name again, so a DNS server that answers
// differently the second time gains nothing.
void ImageLoader::fetchRemote(const QString &src, const QUrl &url, quint64 generation, int redirectsLeft)
{
    if (const QHostAddress literal(url.host()); !literal.isNull()) {
        if (m_addressCheck(literal)) {
            download(src, url, QHostAddress(), generation, redirectsLeft);
        } else {
            finish(src, generation, Decoded());
            downloadDone(generation);
        }
        return;
    }
    QHostInfo::lookupHost(url.host(), this, [this, src, url, generation, redirectsLeft](const QHostInfo &info) {
        const QList<QHostAddress> addresses = info.addresses();
        const bool usable = info.error() == QHostInfo::NoError && !addresses.isEmpty()
            && std::ranges::all_of(addresses, m_addressCheck);
        if (!usable) {
            qCDebug(lcImages) << "Remote image host refused or unresolved:" << url.host();
            finish(src, generation, Decoded());
            downloadDone(generation);
            return;
        }
        download(src, url, addresses.first(), generation, redirectsLeft);
    });
}

// `address` is where `url`'s host resolved to, or null when the host is a
// literal address already.
void ImageLoader::download(const QString &src, const QUrl &url, const QHostAddress &address, quint64 generation,
                           int redirectsLeft)
{
    if (generation != m_generation)
        return;
    if (!m_network) {
        m_network = new QNetworkAccessManager(this);
        m_network->setCookieJar(new NoCookies(m_network));
        // Redirects are followed by hand, so each new host is checked first.
        m_network->setRedirectPolicy(QNetworkRequest::ManualRedirectPolicy);
    }
    QUrl target = url;
    if (!address.isNull())
        target.setHost(address.toString());
    QNetworkRequest request(target);
    if (!address.isNull()) {
        // The name still identifies the site: in the Host header, in the TLS
        // handshake (server name indication) and in certificate checking.
        QByteArray host = url.host(QUrl::FullyEncoded).toUtf8();
        if (url.port() != -1)
            host += ':' + QByteArray::number(url.port());
        request.setRawHeader("Host", host);
        request.setPeerVerifyName(url.host());
    }
    request.setTransferTimeout(DownloadTimeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("markdown-glass"));
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64 total) {
        if (received > MaxDownloadBytes || total > MaxDownloadBytes)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, src, url, generation, redirectsLeft] {
        reply->deleteLater();
        const QUrl redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (reply->error() == QNetworkReply::NoError && redirect.isValid()) {
            // Resolved against the named URL, so a relative redirect keeps the host name.
            const QUrl next = url.resolved(redirect);
            // Never from https to http, as the default policy would also refuse.
            const bool safe = next.scheme() == QLatin1String("https")
                || (next.scheme() == QLatin1String("http") && url.scheme() == next.scheme());
            if (redirectsLeft > 0 && safe && generation == m_generation) {
                fetchRemote(src, next, generation, redirectsLeft - 1);
            } else {
                finish(src, generation, Decoded());
                downloadDone(generation);
            }
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            qCDebug(lcImages) << "Download failed:" << src << reply->errorString();
            finish(src, generation, Decoded());
            downloadDone(generation);
            return;
        }
        const QByteArray data = reply->read(MaxDownloadBytes);
        downloadDone(generation);
        QThreadPool::globalInstance()->start(
            [data, deliver = resultDelivery(src, generation)] { deliver(decode(data)); });
    });
}

void ImageLoader::finish(const QString &src, quint64 generation, const Decoded &decoded)
{
    if (generation != m_generation)
        return;
    const auto it = m_entries.find(src);
    if (it == m_entries.end())
        return;
    if (decoded.ok && m_bytes + decoded.image.sizeInBytes() <= MaxCacheBytes) {
        it->state = Ready;
        it->image = decoded.image;
        it->natural = decoded.natural;
        m_bytes += decoded.image.sizeInBytes() + decoded.animation.size();
        if (!decoded.animation.isEmpty()) {
            auto animation = std::make_shared<Animation>();
            animation->data = decoded.animation;
            if (decoded.image.size() != decoded.natural)
                animation->scaledSize = decoded.image.size();
            animation->rewind();
            animation->loopsLeft = animation->reader.loopCount();
            animation->reader.read(); // the first frame is already showing
            animation->timer.setSingleShot(true);
            connect(&animation->timer, &QTimer::timeout, this, [this, src] { advance(src); });
            it->animation = std::move(animation);
        }
    } else {
        qCDebug(lcImages) << "Image not shown (unreadable, refused or over the cache limit):" << src;
        it->state = Failed;
    }
    m_notify.start();
}
