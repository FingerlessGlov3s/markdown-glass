#pragma once

#include "model/document.h"
#include "model/htmlsubset.h"

#include <QHash>
#include <QList>
#include <QString>

namespace md {

// Accumulates inline text and tracks the active formatting while walking inline nodes.
class InlineBuilder
{
public:
    void appendText(QStringView text)
    {
        if (text.isEmpty())
            return;
        const Span::Flags f = flags();
        const int link = m_links.isEmpty() ? -1 : m_links.last();
        const int start = m_out.text.size();
        m_out.text += text;
        if (!m_out.spans.isEmpty()) {
            Span &last = m_out.spans.last();
            if (last.flags == f && last.link == link) {
                last.length += text.size();
                return;
            }
        }
        m_out.spans.append(Span {start, int(text.size()), f, link});
    }

    // Appends HTML text, collapsing whitespace as a browser would.
    void appendHtmlText(QStringView text)
    {
        QString collapsed;
        collapsed.reserve(text.size());
        bool lastSpace =
            m_out.text.isEmpty() || m_out.text.back().isSpace() || m_out.text.back() == QChar::LineSeparator;
        for (const QChar c : text) {
            const bool space = c.isSpace() && c != QChar::Nbsp;
            if (space) {
                if (!lastSpace)
                    collapsed += u' ';
            } else {
                collapsed += c;
            }
            lastSpace = space;
        }
        appendText(collapsed);
    }

    void appendImage(const QString &src, const QString &alt, int width = -1, int height = -1)
    {
        InlineImage img;
        img.pos = m_out.text.size();
        img.src = src;
        img.alt = alt;
        img.width = width;
        img.height = height;
        img.link = m_links.isEmpty() ? -1 : m_links.last();
        m_out.images.append(img);
        appendText(QStringView(&InlineText::ImagePlaceholder, 1));
    }

    void lineBreak()
    {
        const QChar separator = QChar::LineSeparator;
        appendText(QStringView(&separator, 1));
    }

    void setFlag(Span::Flag flag, bool on)
    {
        int &depth = m_depth[flag];
        depth = qMax(0, depth + (on ? 1 : -1));
    }

    void pushLink(const QString &href)
    {
        m_out.links.append(href);
        m_links.append(m_out.links.size() - 1);
    }

    void popLink()
    {
        if (!m_links.isEmpty())
            m_links.removeLast();
    }

    // Raw HTML <a> and </a>. An <a> without an href pushes no link, so its
    // </a> must not pop one; a stray </a> pops nothing either. Otherwise
    // "[x <a>y</a> z](u)" would end the markdown link early.
    void openHtmlAnchor(const QString &href)
    {
        m_htmlAnchors.append(!href.isEmpty());
        if (!href.isEmpty())
            pushLink(href);
    }

    void closeHtmlAnchor()
    {
        if (!m_htmlAnchors.isEmpty() && m_htmlAnchors.takeLast())
            popLink();
    }

    void resetFormatting()
    {
        m_depth.clear();
        m_links.clear();
        m_htmlAnchors.clear();
    }

    bool hasContent() const
    {
        for (const QChar c : m_out.text) {
            if (!c.isSpace() && c != QChar::LineSeparator)
                return true;
        }
        return false;
    }

    InlineText take()
    {
        // Trim trailing whitespace; spans are clipped to the new length.
        int end = m_out.text.size();
        while (end > 0 && (m_out.text[end - 1].isSpace() || m_out.text[end - 1] == QChar::LineSeparator)
               && m_out.text[end - 1] != QChar::Nbsp)
            --end;
        m_out.text.truncate(end);
        while (!m_out.spans.isEmpty() && m_out.spans.last().start >= end)
            m_out.spans.removeLast();
        if (!m_out.spans.isEmpty())
            m_out.spans.last().length = end - m_out.spans.last().start;

        InlineText out = std::move(m_out);
        m_out = InlineText();
        resetFormatting();
        return out;
    }

private:
    Span::Flags flags() const
    {
        Span::Flags f;
        for (auto it = m_depth.begin(); it != m_depth.end(); ++it) {
            if (it.value() > 0)
                f |= it.key();
        }
        return f;
    }

    InlineText m_out;
    QHash<Span::Flag, int> m_depth; // how many open tags or markers ask for each flag
    QList<int> m_links;
    QList<bool> m_htmlAnchors; // per open <a>: whether it pushed a link
};

// Alignment from an HTML align="..." value, or `fallback` if not recognised.
Align parseAlign(const QString &value, Align fallback);

// The largest width or height an <img> tag may ask for; a limit on what a
// document can make the layout allocate, not a real screen size.
constexpr int MaxImageDimension = 20000;

// Pixel size from an HTML width/height value, or -1 for anything else
// (percentages, units, nonsense, or sizes over MaxImageDimension).
int parseDimension(const QString &value);

// Applies an inline HTML tag (<b>, <a>, <img>, <br>, <kbd>, ...) to the text
// being built. Tags that are not supported inline are ignored.
void applyInlineTag(InlineBuilder &b, const HtmlToken &t);

} // namespace md
