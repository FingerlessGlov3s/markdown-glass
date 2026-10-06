#include "model/htmlsubset.h"

namespace md {

namespace {

struct Entity {
    const char *name;
    char32_t code;
};

// clang-format off: kept as a readable table
constexpr Entity Entities[] = {
    {"amp", U'&'}, {"lt", U'<'}, {"gt", U'>'}, {"quot", U'"'}, {"apos", U'\''},
    {"nbsp", 0x00A0}, {"copy", 0x00A9}, {"reg", 0x00AE}, {"trade", 0x2122},
    {"hellip", 0x2026}, {"mdash", 0x2014}, {"ndash", 0x2013}, {"larr", 0x2190},
    {"rarr", 0x2192}, {"uarr", 0x2191}, {"darr", 0x2193}, {"middot", 0x00B7},
    {"bull", 0x2022}, {"laquo", 0x00AB}, {"raquo", 0x00BB}, {"times", 0x00D7},
    {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D},
    {"check", 0x2713}, {"star", 0x2606}, {"hearts", 0x2665}, {"deg", 0x00B0},
};
// clang-format on

bool isNameChar(QChar c)
{
    return c.isLetterOrNumber() || c == u'-' || c == u'_' || c == u':';
}

// The longest entity between '&' and ';' that can be decoded: "#x10FFFF" and
// every name in the table are shorter. Searching only this far for the ';'
// keeps a run of ampersands linear rather than quadratic.
constexpr qsizetype MaxEntityLength = 11;

} // namespace

QString decodeHtmlEntities(QStringView text)
{
    QString out;
    out.reserve(text.size());
    qsizetype i = 0;
    while (i < text.size()) {
        const QChar c = text[i];
        if (c != u'&') {
            out += c;
            ++i;
            continue;
        }
        const qsizetype length = text.mid(i + 1, MaxEntityLength + 1).indexOf(u';');
        if (length < 0) {
            out += c;
            ++i;
            continue;
        }
        const qsizetype semi = i + 1 + length;
        const QStringView name = text.mid(i + 1, length);
        char32_t code = 0;
        if (name.startsWith(u'#')) {
            bool ok = false;
            const bool hex = name.size() > 1 && (name[1] == u'x' || name[1] == u'X');
            const uint v = hex ? name.mid(2).toUInt(&ok, 16) : name.mid(1).toUInt(&ok, 10);
            if (ok && v > 0 && v <= 0x10FFFF && (v < 0xD800 || v > 0xDFFF)) // not a lone surrogate
                code = v;
        } else {
            for (const Entity &e : Entities) {
                if (name == QLatin1String(e.name)) {
                    code = e.code;
                    break;
                }
            }
        }
        if (code == 0) {
            out += c;
            ++i;
            continue;
        }
        out += QString::fromUcs4(&code, 1);
        i = semi + 1;
    }
    return out;
}

namespace {

// Splits an HTML fragment into text and tags in one left-to-right pass. Every
// path advances the position, so it always terminates, and malformed markup
// degrades to text instead of failing.
class Tokenizer
{
public:
    explicit Tokenizer(QStringView html)
        : m_html(html)
        , m_end(html.size())
    { }

    QList<HtmlToken> run()
    {
        while (m_pos < m_end) {
            // A '<' that starts neither a comment, a declaration nor a tag is plain text.
            if (m_html[m_pos] == u'<' && (skipCommentOrDeclaration() || readTag()))
                continue;
            ++m_pos;
        }
        flushText(m_end);
        return std::move(m_tokens);
    }

private:
    // Emits the text between the last tag and `end`.
    void flushText(qsizetype end)
    {
        if (end > m_textStart) {
            HtmlToken t;
            t.kind = HtmlToken::Text;
            t.text = decodeHtmlEntities(m_html.mid(m_textStart, end - m_textStart));
            m_tokens.append(std::move(t));
        }
    }

    // Moves past markup that is dropped entirely, up to and including `end`.
    void dropUntil(qsizetype end)
    {
        flushText(m_pos);
        m_pos = m_textStart = end;
    }

    // At '<': skips <!-- comments -->, <!DOCTYPE ...> and <?...?>.
    bool skipCommentOrDeclaration()
    {
        if (m_html.mid(m_pos).startsWith(QLatin1String("<!--"))) {
            const qsizetype close = m_html.indexOf(QLatin1String("-->"), m_pos + 4);
            dropUntil(close < 0 ? m_end : close + 3);
            return true;
        }
        if (m_pos + 1 < m_end && (m_html[m_pos + 1] == u'!' || m_html[m_pos + 1] == u'?')) {
            const qsizetype close = m_html.indexOf(u'>', m_pos + 2);
            dropUntil(close < 0 ? m_end : close + 1);
            return true;
        }
        return false;
    }

    // At '<': reads an opening or closing tag. Returns false if this '<' does
    // not start a tag (it is then plain text).
    bool readTag()
    {
        qsizetype p = m_pos + 1;
        const bool closing = p < m_end && m_html[p] == u'/';
        if (closing)
            ++p;
        const qsizetype nameStart = p;
        while (p < m_end && isNameChar(m_html[p]))
            ++p;
        if (p == nameStart || !m_html[nameStart].isLetter())
            return false;

        HtmlToken tag;
        tag.kind = closing ? HtmlToken::Close : HtmlToken::Open;
        tag.name = m_html.mid(nameStart, p - nameStart).toString().toLower();

        const qsizetype afterTag = readAttributes(tag, closing, p);
        if (afterTag < 0) {
            // Unterminated tag: treat the rest as nothing rather than leaking markup.
            dropUntil(m_end);
            return true;
        }
        dropUntil(afterTag);

        if (tag.name == QLatin1String("script") || tag.name == QLatin1String("style")) {
            // Never passed on; an opening one also drops everything up to its end tag.
            if (!closing)
                skipRawText(tag.name);
            return true;
        }
        m_tokens.append(std::move(tag));
        return true;
    }

    // Reads attributes from p up to the closing '>'. Returns the position just
    // after the '>', or -1 if the input ends first. Closing tags have their
    // attributes skipped; repeated attributes keep the first value, as in HTML.
    qsizetype readAttributes(HtmlToken &tag, bool closing, qsizetype p) const
    {
        while (p < m_end) {
            while (p < m_end && (m_html[p].isSpace() || m_html[p] == u'/'))
                ++p;
            if (p >= m_end)
                break;
            if (m_html[p] == u'>')
                return p + 1;

            const qsizetype nameStart = p;
            while (p < m_end && !m_html[p].isSpace() && m_html[p] != u'=' && m_html[p] != u'>' && m_html[p] != u'/')
                ++p;
            if (p == nameStart) {
                ++p; // stray character such as '='
                continue;
            }
            const QString name = m_html.mid(nameStart, p - nameStart).toString().toLower();
            while (p < m_end && m_html[p].isSpace())
                ++p;
            QString value;
            if (p < m_end && m_html[p] == u'=') {
                ++p;
                while (p < m_end && m_html[p].isSpace())
                    ++p;
                value = readAttributeValue(p);
            }
            if (!closing && !tag.attrs.contains(name))
                tag.attrs.insert(name, value);
        }
        return -1;
    }

    // Reads a quoted or bare attribute value starting at p, advancing p past it.
    QString readAttributeValue(qsizetype &p) const
    {
        if (p < m_end && (m_html[p] == u'"' || m_html[p] == u'\'')) {
            const QChar quote = m_html[p];
            const qsizetype close = m_html.indexOf(quote, p + 1);
            const qsizetype stop = close < 0 ? m_end : close;
            const QString value = decodeHtmlEntities(m_html.mid(p + 1, stop - p - 1));
            p = close < 0 ? m_end : close + 1;
            return value;
        }
        const qsizetype start = p;
        while (p < m_end && !m_html[p].isSpace() && m_html[p] != u'>')
            ++p;
        return decodeHtmlEntities(m_html.mid(start, p - start));
    }

    // Drops the contents of <script> or <style> up to and including its end tag.
    void skipRawText(const QString &name)
    {
        const qsizetype close = m_html.indexOf(QLatin1String("</") + name, m_pos, Qt::CaseInsensitive);
        if (close < 0) {
            m_pos = m_textStart = m_end;
            return;
        }
        const qsizetype gt = m_html.indexOf(u'>', close);
        m_pos = m_textStart = gt < 0 ? m_end : gt + 1;
    }

    QStringView m_html;
    qsizetype m_end;
    qsizetype m_pos = 0;
    qsizetype m_textStart = 0; // start of text not yet emitted
    QList<HtmlToken> m_tokens;
};

} // namespace

QList<HtmlToken> tokenizeHtml(QStringView html)
{
    return Tokenizer(html).run();
}

} // namespace md
