#include "model/converter.h"
#include "model/htmlsubset.h"

#include <QSet>

#include <utility>

namespace md {

// Turns one raw HTML block into blocks. Inline tags become formatting, block
// tags end the current paragraph, and <details>, alignment containers and
// headings open scopes. Unsupported tags are dropped and their text kept.
class Converter::HtmlBlockReader
{
public:
    HtmlBlockReader(Converter &converter, Scope &scope, int line)
        : m_converter(converter)
        , m_scope(scope)
        , m_line(line)
    { }

    void read(const QString &html)
    {
        for (const HtmlToken &t : tokenizeHtml(html)) {
            if (t.kind == HtmlToken::Text)
                m_text.appendHtmlText(t.text);
            else
                tag(t);
        }
        flush();
    }

private:
    // What the text being collected will become when it is flushed.
    enum class Mode { Paragraph, Heading, Summary };

    void tag(const HtmlToken &t)
    {
        static const QSet<QString> containers = {
            QStringLiteral("p"),       QStringLiteral("div"),    QStringLiteral("center"), QStringLiteral("section"),
            QStringLiteral("article"), QStringLiteral("header"), QStringLiteral("footer"),
        };
        // Block tags we do not render as such; they still end a paragraph.
        static const QSet<QString> breakers = {
            QStringLiteral("ul"),     QStringLiteral("ol"),         QStringLiteral("li"),
            QStringLiteral("table"),  QStringLiteral("tr"),         QStringLiteral("thead"),
            QStringLiteral("tbody"),  QStringLiteral("blockquote"), QStringLiteral("pre"),
            QStringLiteral("dl"),     QStringLiteral("dt"),         QStringLiteral("dd"),
            QStringLiteral("figure"), QStringLiteral("figcaption"),
        };
        const bool open = t.kind == HtmlToken::Open;
        const QString &name = t.name;

        if (name == QLatin1String("details")) {
            flush();
            open ? openDetails(t) : closeDetails();
        } else if (name == QLatin1String("summary")) {
            flush();
            if (open)
                m_mode = Mode::Summary;
        } else if (containers.contains(name)) {
            flush();
            open ? openContainer(t) : closeContainer();
        } else if (name.size() == 2 && name[0] == u'h' && name[1] >= u'1' && name[1] <= u'6') {
            flush();
            if (open) {
                m_mode = Mode::Heading;
                m_headingLevel = name[1].digitValue();
                m_headingAlign = parseAlign(t.attrs.value(QStringLiteral("align")), m_scope.align());
            }
        } else if (name == QLatin1String("hr")) {
            flush();
            if (open)
                add(std::make_unique<Block>(Block::Rule));
        } else if (breakers.contains(name)) {
            flush();
        } else if (name == QLatin1String("td") || name == QLatin1String("th")) {
            m_text.appendHtmlText(u" ");
        } else {
            applyInlineTag(m_text, t);
        }
    }

    // Ends the text collected so far as a paragraph, heading or summary.
    void flush()
    {
        const Mode mode = std::exchange(m_mode, Mode::Paragraph);
        if (!m_text.hasContent()) {
            m_text.take();
            return;
        }
        if (mode == Mode::Summary && !m_scope.details.isEmpty()) {
            m_scope.details.last()->inl = m_text.take();
        } else if (mode == Mode::Heading) {
            auto block = std::make_unique<Block>(Block::Heading);
            block->level = m_headingLevel;
            block->align = m_headingAlign;
            block->inl = m_text.take();
            m_converter.finishHeading(*block);
            add(std::move(block));
        } else {
            auto block = std::make_unique<Block>(Block::Paragraph);
            block->align = m_scope.align();
            block->inl = m_text.take();
            add(std::move(block));
        }
    }

    void add(std::unique_ptr<Block> block)
    {
        block->sourceLine = m_line;
        m_scope.target().push_back(std::move(block));
    }

    // <details> nests: blocks that follow go inside it until </details>.
    // Unclosed ones count towards the nesting limit until their container ends.
    void openDetails(const HtmlToken &t)
    {
        if (m_converter.m_blockDepth + m_converter.m_openDetails >= MaxDepth)
            return;
        ++m_converter.m_openDetails;
        auto block = std::make_unique<Block>(Block::Details);
        block->open = t.attrs.contains(QStringLiteral("open"));
        Block *details = block.get();
        add(std::move(block));
        m_scope.targets.append(&details->children);
        m_scope.details.append(details);
    }

    void closeDetails()
    {
        if (m_scope.details.isEmpty())
            return;
        --m_converter.m_openDetails;
        m_scope.targets.removeLast();
        m_scope.details.removeLast();
    }

    // <p>, <div align="center"> and friends only matter for their alignment.
    void openContainer(const HtmlToken &t)
    {
        Align align = m_scope.align();
        if (t.name == QLatin1String("center"))
            align = Align::Center;
        m_scope.aligns.append(parseAlign(t.attrs.value(QStringLiteral("align")), align));
    }

    void closeContainer()
    {
        if (m_scope.aligns.size() > 1)
            m_scope.aligns.removeLast();
    }

    Converter &m_converter;
    Scope &m_scope;
    int m_line;
    InlineBuilder m_text;
    Mode m_mode = Mode::Paragraph;
    int m_headingLevel = 1;
    Align m_headingAlign = Align::Default;
};

void Converter::htmlBlock(const QString &html, Scope &scope, int line)
{
    HtmlBlockReader(*this, scope, line).read(html);
}

} // namespace md
