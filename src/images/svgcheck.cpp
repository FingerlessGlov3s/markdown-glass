#include "images/svgcheck.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QXmlStreamReader>

namespace {

// A data: image may itself be an SVG; nesting beyond this is refused.
constexpr int MaxNesting = 4;
// Drawing cost grows with the number of elements, and nothing in a document
// needs an image this detailed.
constexpr int MaxElements = 100'000;
// How many elements drawing may visit through href="#id" and url(#id)
// references, counting a target once for every reference to it. <use> and
// patterns nest, so thirty elements can otherwise ask for a billion draws.
constexpr qint64 MaxReferenceExpansion = 200'000;

// Internal references as a graph: which ids each element with an id refers
// to from inside, and every reference the image makes. The expansion of that
// graph is what drawing the image costs.
class References
{
public:
    void enter(const QString &id) { m_open.append(id); } // empty for elements without one
    void leave() { m_open.removeLast(); }

    void note(QStringView id)
    {
        const QString target = id.toString();
        m_all.append(target);
        for (const QString &open : std::as_const(m_open)) {
            if (!open.isEmpty())
                m_edges[open].append(target);
        }
    }

    bool withinBudget()
    {
        qint64 total = 0;
        for (const QString &target : std::as_const(m_all)) {
            total += cost(target);
            if (total > MaxReferenceExpansion)
                return false;
        }
        return true;
    }

private:
    // Elements drawn when `id` is drawn, through references. A cycle or a
    // cost over the budget both come back as over the budget.
    qint64 cost(const QString &id)
    {
        if (const auto known = m_cost.constFind(id); known != m_cost.constEnd())
            return known.value();
        if (m_visiting.contains(id))
            return MaxReferenceExpansion + 1;
        m_visiting.insert(id);
        qint64 total = 1;
        for (const QString &child : m_edges.value(id)) {
            total += cost(child);
            if (total > MaxReferenceExpansion) {
                total = MaxReferenceExpansion + 1;
                break;
            }
        }
        m_visiting.remove(id);
        m_cost.insert(id, total);
        return total;
    }

    QList<QString> m_open;
    QList<QString> m_all;
    QHash<QString, QList<QString>> m_edges;
    QHash<QString, qint64> m_cost;
    QSet<QString> m_visiting;
};

bool selfContained(QByteArrayView svg, int depth);

bool isRasterImage(const QByteArray &data)
{
    return data.startsWith("\x89PNG") || data.startsWith("\xFF\xD8\xFF") || data.startsWith("GIF8")
        || (data.startsWith("RIFF") && data.mid(8, 4) == "WEBP");
}

// A reference is internal when it names a fragment of this document or
// carries its data inline. QtSvg decodes data: images by content, not by the
// declared type, so their payload is checked as well.
bool internalReference(QStringView ref, int depth, References &refs)
{
    ref = ref.trimmed();
    if (ref.startsWith(u'#')) {
        refs.note(ref.mid(1));
        return true;
    }
    if (!ref.startsWith(QLatin1String("data:"), Qt::CaseInsensitive))
        return false;
    const qsizetype comma = ref.indexOf(u',');
    if (comma < 0)
        return false;
    const QStringView header = ref.left(comma);
    // Some QtSvg versions find the payload after the last "base64" in the
    // reference rather than after the first comma; one that could be read
    // either way might be checked as one payload and drawn as another.
    if (ref.mid(comma + 1).contains(QLatin1String("base64"), Qt::CaseInsensitive))
        return false;
    const QByteArray body = ref.mid(comma + 1).toLatin1();
    const QByteArray payload = header.endsWith(QLatin1String(";base64"), Qt::CaseInsensitive)
        ? QByteArray::fromBase64(body)
        : QByteArray::fromPercentEncoding(body);
    return isRasterImage(payload) || selfContained(payload, depth + 1);
}

// Checks style sheet text: inline style attributes and <style> elements.
// Backslashes are refused outright because CSS escapes could spell "url" or
// "@import" in a form this scan would not see.
bool cleanCss(QStringView css, int depth, References &refs)
{
    if (css.contains(u'\\') || css.contains(QLatin1String("@import"), Qt::CaseInsensitive))
        return false;
    qsizetype from = 0;
    while (true) {
        const qsizetype at = css.indexOf(QLatin1String("url("), from, Qt::CaseInsensitive);
        if (at < 0)
            return true;
        const qsizetype end = css.indexOf(u')', at);
        if (end < 0)
            return false;
        QStringView ref = css.mid(at + 4, end - at - 4).trimmed();
        if (ref.size() >= 2 && (ref.front() == u'"' || ref.front() == u'\'') && ref.back() == ref.front())
            ref = ref.mid(1, ref.size() - 2);
        if (!internalReference(ref, depth, refs))
            return false;
        from = end + 1;
    }
}

bool selfContained(QByteArrayView svg, int depth)
{
    if (depth > MaxNesting)
        return false;
    QXmlStreamReader xml(svg.toByteArray());
    References refs;
    bool inStyle = false;
    int elements = 0;
    while (!xml.atEnd()) {
        switch (xml.readNext()) {
        case QXmlStreamReader::DTD:
            // A DOCTYPE naming the SVG DTD is common and harmless (it is never
            // fetched), but declared entities could hide references.
            if (!xml.entityDeclarations().isEmpty())
                return false;
            break;
        case QXmlStreamReader::EntityReference:
            return false;
        case QXmlStreamReader::ProcessingInstruction:
            if (xml.processingInstructionTarget().compare(QLatin1String("xml-stylesheet"), Qt::CaseInsensitive) == 0)
                return false;
            break;
        case QXmlStreamReader::StartElement: {
            if (++elements > MaxElements)
                return false;
            inStyle = xml.name() == QLatin1String("style");
            const QXmlStreamAttributes attributes = xml.attributes();
            refs.enter(attributes.value(QLatin1String("id")).toString());
            for (const QXmlStreamAttribute &attribute : attributes) {
                // Matched by local name, so any namespace prefix (xlink:, or
                // one the document invents) is covered.
                if (attribute.name() == QLatin1String("href") && !internalReference(attribute.value(), depth, refs))
                    return false;
                // Presentation attributes such as fill and filter take url() too.
                if (!cleanCss(attribute.value(), depth, refs))
                    return false;
            }
            break;
        }
        case QXmlStreamReader::EndElement:
            inStyle = false;
            refs.leave();
            break;
        case QXmlStreamReader::Characters:
            if (inStyle && !cleanCss(xml.text(), depth, refs))
                return false;
            break;
        default:
            break;
        }
    }
    return !xml.hasError() && refs.withinBudget();
}

} // namespace

bool svgIsSelfContained(QByteArrayView svg)
{
    return selfContained(svg, 0);
}
