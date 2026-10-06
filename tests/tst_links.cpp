#include "app/links.h"
#include "testsupport.h"

#include <QTemporaryDir>
#include <QTest>

// Classifying links found in documents, including the ones that must never
// be launched.
class TestLinks : public QObject
{
    Q_OBJECT
private slots:
    void resolve();
};

void TestLinks::resolve()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString base = dir.path();
    for (const char *name : {"other.md", "run.sh", "My Notes.md"})
        writeFile(base, QLatin1String(name), QByteArray());

    QCOMPARE(resolveLink(QStringLiteral("#intro"), base).kind, LinkTarget::Anchor);
    QCOMPARE(resolveLink(QStringLiteral("#caf%C3%A9"), base).anchor, QStringLiteral("café"));

    const LinkTarget other = resolveLink(QStringLiteral("other.md#usage"), base);
    QCOMPARE(other.kind, LinkTarget::Markdown);
    QCOMPARE(other.path, base + QStringLiteral("/other.md"));
    QCOMPARE(other.anchor, QStringLiteral("usage"));
    QCOMPARE(resolveLink(QStringLiteral("My%20Notes.md"), base).kind, LinkTarget::Markdown);
    QCOMPARE(resolveLink(QStringLiteral("./sub/../other.md"), base).path, base + QStringLiteral("/other.md"));

    QCOMPARE(resolveLink(QStringLiteral("https://example.com/a?b#c"), base).kind, LinkTarget::Web);
    QCOMPARE(resolveLink(QStringLiteral("mailto:a@example.com"), base).kind, LinkTarget::Mail);
    // Only the recipients, subject and body reach the mail client.
    QCOMPARE(resolveLink(QStringLiteral("mailto:a@example.com?Subject=Hi%20there&attach=/etc/passwd&bcc=x@y.test"
                                        "&body=b&attachment=~/.ssh/id_ed25519"),
                         base)
                 .url.toString(QUrl::FullyEncoded),
             QStringLiteral("mailto:a@example.com?Subject=Hi%20there&body=b"));
    QCOMPARE(resolveLink(QStringLiteral("mailto:a@example.com?attach=/etc/passwd"), base).url.toString(),
             QStringLiteral("mailto:a@example.com"));
    // One recipient only, however the others are separated or encoded.
    for (const char *many :
         {"mailto:a@example.com,b@example.com", "mailto:a@example.com;b@example.com",
          "mailto:a@example.com%2Cb@example.com", "mailto:a@example.com%3B%20b@example.com?body=x"}) {
        const QUrl url = resolveLink(QString::fromLatin1(many), base).url;
        QCOMPARE(url.path(), QStringLiteral("a@example.com"));
        QVERIFY(!url.toString(QUrl::FullyEncoded).contains(QLatin1String("b@example")));
    }

    // Things that must never be launched directly.
    QCOMPARE(resolveLink(QStringLiteral("run.sh"), base).kind, LinkTarget::OtherFile);
    QCOMPARE(resolveLink(QStringLiteral("missing.md"), base).kind, LinkTarget::OtherFile);
    QCOMPARE(resolveLink(QStringLiteral("file:///usr/bin/env"), base).kind, LinkTarget::OtherFile);
    QCOMPARE(resolveLink(QStringLiteral("/etc/passwd"), base).kind, LinkTarget::OtherFile);
    QCOMPARE(resolveLink(QStringLiteral("javascript:alert(1)"), base).kind, LinkTarget::Unsupported);
    QCOMPARE(resolveLink(QStringLiteral("data:text/html,<b>x</b>"), base).kind, LinkTarget::Unsupported);
    QCOMPARE(resolveLink(QStringLiteral("smb://host/share"), base).kind, LinkTarget::Unsupported);
    QCOMPARE(resolveLink(QStringLiteral("//host/path.md"), base).kind, LinkTarget::Unsupported);
    QCOMPARE(resolveLink(QStringLiteral("https:///nohost"), base).kind, LinkTarget::Unsupported);
    QCOMPARE(resolveLink(QString(), base).kind, LinkTarget::Unsupported);
}

QTEST_GUILESS_MAIN(TestLinks)
#include "tst_links.moc"
