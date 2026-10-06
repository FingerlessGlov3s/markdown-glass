#include "images/imageloader.h"
#include "images/publicaddress.h"
#include "images/svgcheck.h"

#include <QBuffer>
#include <QHostAddress>
#include <QPainter>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <memory>

class TestImages : public QObject
{
    Q_OBJECT
private slots:
    void decodeAcceptsAllowedFormats();
    void decodeRejectsUntrusted();
    void svgExternalReferences_data();
    void svgExternalReferences();
    void tallImagesAreCapped();
    void publicAddresses_data();
    void publicAddresses();
    void localImagesLoad();
    void remoteImagesBlockedByDefault();
    void remoteImagesDownload();
    void remoteDownloadsAreQueued();
    void animatedGifPlaysAndCanBeSwitchedOff();
    void previewPaintingDoesNotAnimate();
    void clearDropsDecodesInFlight();
};

// A small HTTP server on this machine, for the download paths. The loader
// refuses this machine by design, so tests swap its address check.
class TinyHttpServer : public QObject
{
public:
    TinyHttpServer()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection())
                serve(socket);
        });
        if (!m_server.listen(QHostAddress::Any))
            qFatal("cannot listen");
    }

    quint16 port() const { return m_server.serverPort(); }
    QString url(const QString &path, const QString &host = QStringLiteral("localhost")) const
    {
        return QStringLiteral("http://%1:%2%3").arg(host).arg(port()).arg(path);
    }

    QByteArray png;
    int delayMs = 0;
    int open = 0;
    int peak = 0;
    QByteArrayList hosts; // the Host header of every request

private:
    void serve(QTcpSocket *socket)
    {
        peak = qMax(peak, ++open);
        auto request = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, request] {
            request->append(socket->readAll());
            const qsizetype end = request->indexOf("\r\n\r\n");
            if (end < 0)
                return;
            const QByteArray path = request->split(' ').value(1);
            for (const QByteArray &line : request->left(end).split('\n')) {
                if (line.toLower().startsWith("host:"))
                    hosts.append(line.mid(5).trimmed());
            }
            QTimer::singleShot(delayMs, socket, [this, socket, path] {
                socket->write(response(path));
                socket->disconnectFromHost();
            });
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            --open;
            socket->deleteLater();
        });
    }

    QByteArray response(const QByteArray &path) const
    {
        if (path.startsWith("/redirect"))
            return "HTTP/1.1 302 Found\r\nLocation: /image.png\r\nContent-Length: 0\r\n\r\n";
        if (path.startsWith("/loop"))
            return "HTTP/1.1 302 Found\r\nLocation: /loop\r\nContent-Length: 0\r\n\r\n";
        if (path.startsWith("/huge"))
            return "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: 30000000\r\n\r\nxx";
        if (path.startsWith("/text"))
            return "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 5\r\n\r\nhello";
        return "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: " + QByteArray::number(png.size())
            + "\r\n\r\n" + png;
    }

    QTcpServer m_server;
};

static QByteArray pngBytes(int w, int h)
{
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return data;
}

void TestImages::decodeAcceptsAllowedFormats()
{
    const auto png = ImageLoader::decode(pngBytes(40, 20));
    QVERIFY(png.ok);
    QCOMPARE(png.natural, QSize(40, 20));

    const auto svg = ImageLoader::decode(
        "<svg xmlns='http://www.w3.org/2000/svg' width='30' height='10'><rect width='30' height='10'/></svg>");
    QVERIFY(svg.ok);
    QCOMPARE(svg.natural, QSize(30, 10));

    // Large images are stored scaled down but report their real size.
    const auto big = ImageLoader::decode(pngBytes(5000, 100));
    QVERIFY(big.ok);
    QCOMPARE(big.natural, QSize(5000, 100));
    QVERIFY(big.image.width() <= 2400);
}

void TestImages::decodeRejectsUntrusted()
{
    QVERIFY(!ImageLoader::decode("not an image at all").ok);
    QVERIFY(!ImageLoader::decode(QByteArray()).ok);
    QVERIFY(!ImageLoader::decode(QByteArray(100000, '\0')).ok);

    // A BMP is a real image but not on the allow-list.
    QImage image(4, 4, QImage::Format_RGB32);
    QByteArray bmp;
    QBuffer buffer(&bmp);
    buffer.open(QIODevice::WriteOnly);
    QVERIFY(image.save(&buffer, "BMP"));
    QVERIFY(!ImageLoader::decode(bmp).ok);

    // SVG that pulls in another file.
    QVERIFY(
        !ImageLoader::decode(
             "<svg xmlns='http://www.w3.org/2000/svg' xmlns:xlink='http://www.w3.org/1999/xlink' width='9' height='9'>"
             "<image xlink:href='file:///etc/passwd' width='9' height='9'/></svg>")
             .ok);

    // Header claiming absurd dimensions (decompression bomb).
    QByteArray bomb = pngBytes(8, 8);
    bomb.replace(16, 8, QByteArray::fromHex("0000ea600000ea60")); // 60000 x 60000
    QVERIFY(!ImageLoader::decode(bomb).ok);
}

void TestImages::svgExternalReferences_data()
{
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<bool>("allowed");
    const QByteArray png = pngBytes(2, 2).toBase64();

    QTest::newRow("plain shapes") << QByteArray("<rect width='9' height='9' fill='url(#g)'/>") << true;
    QTest::newRow("internal use") << QByteArray("<use href='#a'/><use xlink:href=' #b'/>") << true;
    QTest::newRow("inline png") << "<image href='data:image/png;base64," + png + "'/>" << true;
    QTest::newRow("style with internal url")
        << QByteArray("<style>rect { fill: url( '#g' ) }</style><rect style='fill:url(#g)'/>") << true;

    QTest::newRow("xlink file") << QByteArray("<image xlink:href='file:///etc/passwd'/>") << false;
    QTest::newRow("relative") << QByteArray("<image href='secret.png'/>") << false;
    QTest::newRow("prefixed element") << QByteArray("<svg:image xmlns:svg='http://www.w3.org/2000/svg' "
                                                    "href='/etc/passwd'/>")
                                      << false;
    QTest::newRow("> in earlier attribute") << QByteArray("<image title='a>b' href='/etc/passwd'/>") << false;
    QTest::newRow("invented prefix") << QByteArray("<image xmlns:q='http://www.w3.org/1999/xlink' q:href='/x.png'/>")
                                     << false;
    QTest::newRow("feImage") << QByteArray("<filter><feImage href='/x.png'/></filter>") << false;
    QTest::newRow("css url") << QByteArray("<rect style='fill:url(/x.svg#p)'/>") << false;
    QTest::newRow("filter attribute") << QByteArray("<rect filter='url(other.svg#f)'/>") << false;
    QTest::newRow("css import") << QByteArray("<style>@import 'x.css';</style>") << false;
    QTest::newRow("css escape") << QByteArray("<style>rect { fill: u\\72l(/x) }</style>") << false;
    QTest::newRow("nested svg data") << "<image href='data:image/svg+xml;base64,"
            + QByteArray("<svg xmlns='http://www.w3.org/2000/svg'>"
                         "<image href='/etc/passwd'/></svg>")
                  .toBase64()
            + "'/>" << false;
    QTest::newRow("data read two ways") << "<image href='data:image/png;base64," + png + "#base64,PHN2Zz48L3N2Zz4='/>"
                                        << false;
    QTest::newRow("malformed") << QByteArray("<image href='#a'>") << false;

    // Drawing cost: references may fan out, but not explode.
    QByteArray manyUses = "<rect id='r' width='1' height='1'/>";
    for (int i = 0; i < 500; ++i)
        manyUses += "<use href='#r' x='" + QByteArray::number(i) + "'/>";
    QTest::newRow("many uses of one shape") << manyUses << true;
    QByteArray chain = "<rect id='a0' width='1' height='1'/>";
    for (int i = 1; i <= 30; ++i) {
        const QByteArray previous = "#a" + QByteArray::number(i - 1);
        chain +=
            "<g id='a" + QByteArray::number(i) + "'><use href='" + previous + "'/><use href='" + previous + "'/></g>";
    }
    QTest::newRow("doubling chain") << chain + "<use href='#a30'/>" << false;
    QTest::newRow("reference cycle") << QByteArray("<g id='a'><use href='#b'/></g><g id='b'><use href='#a'/></g>")
                                     << false;
    QTest::newRow("pattern fan-out") << QByteArray("<pattern id='p'><rect fill='url(#q)'/></pattern>"
                                                   "<pattern id='q'><rect fill='url(#p)'/></pattern>")
                                     << false;
    QTest::newRow("too many elements") << QByteArray("<g/>").repeated(100'001) << false;
}

void TestImages::svgExternalReferences()
{
    QFETCH(QByteArray, body);
    QFETCH(bool, allowed);
    const QByteArray svg = "<svg xmlns='http://www.w3.org/2000/svg' xmlns:xlink='http://www.w3.org/1999/xlink' "
                           "width='9' height='9'>"
        + body + "</svg>";
    QCOMPARE(svgIsSelfContained(svg), allowed);

    // Outside the document element: entities and style sheet instructions.
    QVERIFY(!svgIsSelfContained("<!DOCTYPE svg [<!ENTITY x SYSTEM '/etc/passwd'>]><svg>&x;</svg>"));
    QVERIFY(!svgIsSelfContained("<?xml-stylesheet href='x.css'?><svg/>"));
    QVERIFY(svgIsSelfContained("<!DOCTYPE svg PUBLIC '-//W3C//DTD SVG 1.1//EN' "
                               "'http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd'><svg/>"));
}

void TestImages::tallImagesAreCapped()
{
    // Narrow enough to pass the width limit, but tall: capped by area too.
    const auto tall = ImageLoader::decode(pngBytes(500, 34000));
    QVERIFY(tall.ok);
    QCOMPARE(tall.natural, QSize(500, 34000));
    QVERIFY(qint64(tall.image.width()) * tall.image.height() <= 16'000'000);

    // The rule itself, for shapes too big to decode in a test.
    auto area = [](QSize s) { return qint64(s.width()) * s.height(); };
    QCOMPARE(storedImageSize(QSize(800, 600)), QSize(800, 600));
    QCOMPARE(storedImageSize(QSize(4800, 100)), QSize(2400, 50));
    QVERIFY(area(storedImageSize(QSize(2000, 40000))) <= 16'000'000);
    // One pixel wide: the width cannot shrink further, so the height is cut.
    const QSize needle = storedImageSize(QSize(1, 99'999'999));
    QCOMPARE(needle.width(), 1);
    QVERIFY(area(needle) <= 16'000'000);
}

void TestImages::publicAddresses_data()
{
    QTest::addColumn<QString>("address");
    QTest::addColumn<bool>("isPublic");
    for (const char *a :
         {"93.184.215.14", "1.1.1.1", "2606:4700::1111", "::ffff:8.8.8.8", "64:ff9b::808:808", "::8.8.8.8"})
        QTest::newRow(a) << QString::fromLatin1(a) << true;
    for (const char *a : {"127.0.0.1",
                          "10.1.2.3",
                          "172.16.0.1",
                          "192.168.1.1",
                          "169.254.169.254",
                          "100.64.0.1",
                          "0.0.0.0",
                          "255.255.255.255",
                          "224.0.0.1",
                          "::1",
                          "::",
                          "fe80::1",
                          "fd00::1",
                          "fec0::1",
                          "::ffff:127.0.0.1",
                          "::ffff:192.168.0.1",
                          "64:ff9b::7f00:1",
                          "2002:c0a8:0101::1",
                          "::127.0.0.1",
                          "::10.0.0.1",
                          "64:ff9b:1::808:808",
                          "2001::1"})
        QTest::newRow(a) << QString::fromLatin1(a) << false;
}

void TestImages::publicAddresses()
{
    QFETCH(QString, address);
    QFETCH(bool, isPublic);
    QCOMPARE(isPublicAddress(QHostAddress(address)), isPublic);
}

void TestImages::localImagesLoad()
{
    ImageLoader loader;
    loader.setBaseDir(QStringLiteral(SAMPLES_DIR));
    QSignalSpy spy(&loader, &ImageLoader::changed);

    QCOMPARE(loader.request(QStringLiteral("gradient.png")), md::ImageSource::Pending);
    QCOMPARE(loader.request(QStringLiteral("nope.png")), md::ImageSource::Pending);
    QCOMPARE(loader.request(QStringLiteral("/dev/zero")), md::ImageSource::Pending);
    QVERIFY(spy.wait(5000));
    QTRY_COMPARE(loader.request(QStringLiteral("gradient.png")), md::ImageSource::Ready);
    QCOMPARE(loader.naturalSize(QStringLiteral("gradient.png")), QSizeF(120, 60));
    QTRY_COMPARE(loader.request(QStringLiteral("nope.png")), md::ImageSource::Failed);
    QTRY_COMPARE(loader.request(QStringLiteral("/dev/zero")), md::ImageSource::Failed);
    QCOMPARE(loader.request(QStringLiteral("ftp://host/x.png")), md::ImageSource::Failed);
}

void TestImages::remoteImagesBlockedByDefault()
{
    ImageLoader loader;
    QSignalSpy blocked(&loader, &ImageLoader::blockedChanged);
    QVERIFY(!loader.hasBlocked());
    QCOMPARE(loader.request(QStringLiteral("https://example.invalid/a.png")), md::ImageSource::Blocked);
    QCOMPARE(loader.request(QStringLiteral("//example.invalid/b.png")), md::ImageSource::Blocked);
    QVERIFY(loader.hasBlocked());
    QCOMPARE(blocked.size(), 1);

    // Allowing clears the blocked state. Addresses on this machine or the
    // local network are still refused, before anything is sent.
    loader.setRemoteAllowed(true);
    QVERIFY(!loader.hasBlocked());
    QCOMPARE(loader.request(QStringLiteral("https://127.0.0.1/a.png")), md::ImageSource::Failed);
    QCOMPARE(loader.request(QStringLiteral("http://[::1]:8080/a.png")), md::ImageSource::Failed);
    QCOMPARE(loader.request(QStringLiteral("//192.168.1.1/router.png")), md::ImageSource::Failed);
    // A host name is resolved first (here from /etc/hosts, so no network is
    // used) and refused when it leads to this machine.
    QCOMPARE(loader.request(QStringLiteral("https://localhost/a.png")), md::ImageSource::Pending);
    QTRY_COMPARE(loader.request(QStringLiteral("https://localhost/a.png")), md::ImageSource::Failed);
}

void TestImages::remoteImagesDownload()
{
    TinyHttpServer server;
    server.png = pngBytes(12, 7);
    ImageLoader loader;
    loader.setAddressCheck([](const QHostAddress &) { return true; });
    loader.setRemoteAllowed(true);

    // Fetched from the address the name resolved to, with the name itself
    // still sent to the server.
    const QString image = server.url(QStringLiteral("/image.png"));
    QCOMPARE(loader.request(image), md::ImageSource::Pending);
    QTRY_COMPARE_WITH_TIMEOUT(loader.request(image), md::ImageSource::Ready, 5000);
    QCOMPARE(loader.naturalSize(image), QSizeF(12, 7));
    QCOMPARE(server.hosts.value(0), QByteArray("localhost:") + QByteArray::number(server.port()));

    // A redirect is followed (through the same checks); a loop gives up; a
    // response that is too big or not an image fails.
    const QString redirect = server.url(QStringLiteral("/redirect"));
    const QString loop = server.url(QStringLiteral("/loop"));
    const QString huge = server.url(QStringLiteral("/huge"));
    const QString text = server.url(QStringLiteral("/text"));
    for (const QString &src : {redirect, loop, huge, text})
        QCOMPARE(loader.request(src), md::ImageSource::Pending);
    QTRY_COMPARE_WITH_TIMEOUT(loader.request(redirect), md::ImageSource::Ready, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(loader.request(loop), md::ImageSource::Failed, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(loader.request(huge), md::ImageSource::Failed, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(loader.request(text), md::ImageSource::Failed, 5000);

    // Without the test's override, the server on this machine is refused.
    loader.setAddressCheck(nullptr);
    const QString refused = server.url(QStringLiteral("/image.png?again"));
    QCOMPARE(loader.request(refused), md::ImageSource::Pending);
    QTRY_COMPARE_WITH_TIMEOUT(loader.request(refused), md::ImageSource::Failed, 5000);
}

void TestImages::remoteDownloadsAreQueued()
{
    TinyHttpServer server;
    server.png = pngBytes(3, 3);
    server.delayMs = 300; // long enough for every allowed download to be open at once
    ImageLoader loader;
    loader.setAddressCheck([](const QHostAddress &) { return true; });
    loader.setRemoteAllowed(true);

    // Each image on its own host, so Qt's own limit of connections per host
    // does not get in the way: every loopback address reaches the server.
    QStringList sources;
    for (int i = 1; i <= 40; ++i)
        sources.append(server.url(QStringLiteral("/image.png"), QStringLiteral("127.0.0.%1").arg(i)));
    for (const QString &src : sources)
        QCOMPARE(loader.request(src), md::ImageSource::Pending);
    for (const QString &src : sources)
        QTRY_COMPARE_WITH_TIMEOUT(loader.request(src), md::ImageSource::Ready, 15000);
    QVERIFY2(server.peak <= 20, qPrintable(QString::number(server.peak)));
    QVERIFY2(server.peak > 6, qPrintable(QString::number(server.peak)));

    // Forgetting the document also forgets what was still waiting.
    server.peak = 0;
    for (int i = 41; i <= 60; ++i)
        loader.request(server.url(QStringLiteral("/image.png"), QStringLiteral("127.0.0.%1").arg(i)));
    loader.clear();
    QTest::qWait(500); // a fixed wait: this checks that nothing more happens
    QVERIFY(server.peak <= 20);
    QCOMPARE(loader.request(sources.first()), md::ImageSource::Pending); // a fresh request after clear()
}

void TestImages::animatedGifPlaysAndCanBeSwitchedOff()
{
    // anim.gif: 8x8, two frames (red then blue), 30 ms each, looping forever.
    QFile file(QStringLiteral(SAMPLES_DIR "/anim.gif"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto decoded = ImageLoader::decode(file.readAll());
    QVERIFY(decoded.ok);
    QVERIFY(!decoded.animation.isEmpty());
    QVERIFY(ImageLoader::decode(pngBytes(4, 4)).animation.isEmpty());

    ImageLoader loader;
    loader.setBaseDir(QStringLiteral(SAMPLES_DIR));
    const QString src = QStringLiteral("anim.gif");
    loader.request(src);
    QTRY_COMPARE(loader.request(src), md::ImageSource::Ready);

    QImage canvas(2, 2, QImage::Format_RGB32);
    auto shown = [&] {
        canvas.fill(Qt::white);
        QPainter p(&canvas);
        loader.paint(p, src, QRectF(0, 0, 2, 2), false);
        p.end();
        return QColor(canvas.pixel(0, 0));
    };
    const QColor first = shown();
    QSignalSpy frames(&loader, &ImageLoader::frameChanged);
    // Painting keeps it alive, as a visible image would.
    QTRY_VERIFY_WITH_TIMEOUT(shown() != first, 3000);
    QVERIFY(frames.size() >= 1);

    loader.setAnimated(false);
    QCOMPARE(shown(), first);
    frames.clear();
    QTest::qWait(300); // a fixed wait: this checks that no frame advances
    QCOMPARE(shown(), first);
    QCOMPARE(frames.size(), 0);

    loader.setAnimated(true);
    QTRY_VERIFY_WITH_TIMEOUT(shown() != first, 3000);
}

void TestImages::previewPaintingDoesNotAnimate()
{
    ImageLoader loader;
    loader.setBaseDir(QStringLiteral(SAMPLES_DIR));
    const QString src = QStringLiteral("anim.gif");
    loader.request(src);
    QTRY_COMPARE(loader.request(src), md::ImageSource::Ready);

    QImage canvas(2, 2, QImage::Format_RGB32);
    auto paint = [&](bool preview) {
        canvas.fill(Qt::white);
        QPainter p(&canvas);
        loader.paint(p, src, QRectF(0, 0, 2, 2), preview);
        p.end();
        return QColor(canvas.pixel(0, 0));
    };
    const QColor first = paint(true);
    QVERIFY(first != QColor(Qt::white)); // the frame is drawn all the same
    QSignalSpy frames(&loader, &ImageLoader::frameChanged);
    // A minimap or a print keeps painting without anyone watching the image.
    for (int i = 0; i < 10; ++i) {
        QCOMPARE(paint(true), first);
        QTest::qWait(30); // a fixed wait: this checks that no frame advances
    }
    QCOMPARE(frames.size(), 0);

    // The real view starts it.
    paint(false);
    QTRY_VERIFY_WITH_TIMEOUT(paint(false) != first, 3000);
    QVERIFY(frames.size() >= 1);
}

void TestImages::clearDropsDecodesInFlight()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // Big enough that decoding on the worker outlasts the clear() below.
    const QString path = dir.filePath(QStringLiteral("big.png"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(pngBytes(3000, 3000));
    }
    ImageLoader loader;
    loader.setBaseDir(dir.path());
    QSignalSpy changed(&loader, &ImageLoader::changed);
    const QString src = QStringLiteral("big.png");
    QCOMPARE(loader.request(src), md::ImageSource::Pending);
    loader.clear();

    // The result belongs to the forgotten document: no entry, no announcement.
    QTest::qWait(1500); // a fixed wait: this checks that nothing arrives
    QCOMPARE(changed.size(), 0);
    QVERIFY(!loader.naturalSize(src).isValid());

    // A fresh request after clear() loads it again.
    QCOMPARE(loader.request(src), md::ImageSource::Pending);
    QTRY_COMPARE_WITH_TIMEOUT(loader.request(src), md::ImageSource::Ready, 10000);
    QCOMPARE(loader.naturalSize(src), QSizeF(3000, 3000));
}

QTEST_MAIN(TestImages)
#include "tst_images.moc"
