#include "app/links.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrlQuery>

bool isMarkdownFile(const QString &path)
{
    static const QStringList suffixes = {
        QStringLiteral("md"),  QStringLiteral("markdown"), QStringLiteral("mdown"),
        QStringLiteral("mkd"), QStringLiteral("mkdn"),     QStringLiteral("mdwn"),
    };
    return suffixes.contains(QFileInfo(path).suffix().toLower());
}

LinkTarget resolveLink(const QString &rawHref, const QString &baseDir)
{
    LinkTarget target;
    const QString href = rawHref.trimmed();
    if (href.isEmpty())
        return target;

    if (href.startsWith(u'#')) {
        target.kind = LinkTarget::Anchor;
        target.anchor = QUrl::fromPercentEncoding(href.mid(1).toUtf8());
        return target;
    }

    const QUrl url(href);
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("http") || scheme == QLatin1String("https")) {
        target.kind = url.isValid() && !url.host().isEmpty() ? LinkTarget::Web : LinkTarget::Unsupported;
        target.url = url;
        return target;
    }
    if (scheme == QLatin1String("mailto")) {
        // Mail clients honour more than the address: some attach the files
        // named by ?attach=, or add hidden recipients. A document only gets
        // to suggest one person to write to, a subject and a body. Clients
        // split the address part on commas (and some on semicolons) after
        // decoding it, so it is cut at the first of either, decoded.
        QUrlQuery kept;
        const auto items = QUrlQuery(url).queryItems(QUrl::FullyEncoded);
        for (const auto &[key, value] : items) {
            if (key.compare(QLatin1String("subject"), Qt::CaseInsensitive) == 0
                || key.compare(QLatin1String("body"), Qt::CaseInsensitive) == 0)
                kept.addQueryItem(key, value);
        }
        QString address = url.path(QUrl::FullyDecoded);
        if (const qsizetype cut = address.indexOf(QRegularExpression(QStringLiteral("[,;]"))); cut >= 0)
            address.truncate(cut);
        target.kind = LinkTarget::Mail;
        target.url = url;
        target.url.setPath(address.trimmed(), QUrl::DecodedMode);
        target.url.setFragment(QString());
        target.url.setQuery(kept.isEmpty() ? QString() : kept.query(QUrl::FullyEncoded));
        return target;
    }

    QString path;
    if (scheme == QLatin1String("file")) {
        path = url.toLocalFile();
    } else if (scheme.isEmpty() && !href.startsWith(QLatin1String("//"))) {
        path = url.path(QUrl::FullyDecoded);
        if (path.isEmpty())
            return target;
        path = QDir(baseDir).absoluteFilePath(path);
    } else {
        return target; // javascript:, data:, custom schemes, protocol-relative URLs...
    }

    target.path = QDir::cleanPath(path);
    target.anchor = url.fragment(QUrl::FullyDecoded);
    target.kind =
        isMarkdownFile(target.path) && QFileInfo(target.path).isFile() ? LinkTarget::Markdown : LinkTarget::OtherFile;
    return target;
}
