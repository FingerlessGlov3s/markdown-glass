#include "model/document.h"

namespace md {

namespace {

const Block *findAnchorIn(const std::vector<std::unique_ptr<Block>> &blocks, const QString &anchor)
{
    for (const auto &b : blocks) {
        if (!b->anchor.isEmpty() && b->anchor == anchor)
            return b.get();
        if (const Block *found = findAnchorIn(b->children, anchor))
            return found;
    }
    return nullptr;
}

} // namespace

const Block *Document::findAnchor(const QString &anchor) const
{
    return findAnchorIn(blocks, anchor);
}

QList<QStringView> codeLines(const QString &code)
{
    QList<QStringView> lines;
    qsizetype pos = 0;
    while (pos <= code.size()) {
        qsizetype end = code.indexOf(u'\n', pos);
        if (end < 0)
            end = code.size();
        lines.append(QStringView(code).mid(pos, end - pos));
        pos = end + 1;
    }
    return lines;
}

QString footnoteAnchor(int number)
{
    return QStringLiteral("fn-") + QString::number(number);
}

QString slugify(const QString &title)
{
    QString out;
    out.reserve(title.size());
    // By code point, not QChar: a letter outside the basic plane is a
    // surrogate pair, which would otherwise be dropped from the anchor.
    for (const char32_t c : title.toUcs4()) {
        if (QChar::isLetterOrNumber(c) || c == U'_' || c == U'-')
            out += QStringView(QChar::fromUcs4(QChar::toLower(c)));
        else if (QChar::isSpace(c))
            out += u'-';
    }
    return out;
}

bool isRemoteUrl(const QString &url)
{
    return url.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
        || url.startsWith(QLatin1String("https://"), Qt::CaseInsensitive) || url.startsWith(QLatin1String("//"));
}

} // namespace md
