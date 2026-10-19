#include "layout/layoutinternal.h"

// LayoutBuilder: walks the document model once and produces the positioned
// text boxes, decorations, scroll frames and click areas of a Layout.
// Spacing follows GitHub's markdown stylesheet, in multiples of the body
// font size (em).

#include "highlight/highlighter.h"

#include <QFontMetricsF>
#include <QThread>

#include <cmath>
#include <system_error>
#include <thread>

namespace md {

namespace {

Qt::Alignment toQt(Align a, Qt::Alignment fallback = Qt::AlignLeft)
{
    switch (a) {
    case Align::Left:
        return Qt::AlignLeft;
    case Align::Center:
        return Qt::AlignHCenter;
    case Align::Right:
        return Qt::AlignRight;
    case Align::Default:
        break;
    }
    return fallback;
}

// Shares `available` width between table columns. Every column gets at least
// its narrowest width and at most its widest; in between, the spare width is
// split in proportion to how much each column could still grow.
QList<qreal> shareColumnWidths(const QList<qreal> &minWidth, const QList<qreal> &maxWidth, qreal available)
{
    qreal sumMin = 0;
    qreal sumMax = 0;
    for (qsizetype c = 0; c < minWidth.size(); ++c) {
        sumMin += minWidth[c];
        sumMax += maxWidth[c];
    }
    QList<qreal> widths(minWidth.size());
    for (qsizetype c = 0; c < minWidth.size(); ++c) {
        if (sumMax <= available)
            widths[c] = maxWidth[c];
        else if (sumMin >= available)
            widths[c] = minWidth[c];
        else
            widths[c] = minWidth[c] + (maxWidth[c] - minWidth[c]) * (available - sumMin) / (sumMax - sumMin);
    }
    return widths;
}

// Shown for a <details> block without a <summary>, as browsers do.
const InlineText &defaultSummary()
{
    static const InlineText summary = [] {
        InlineText t;
        t.text = QStringLiteral("Details");
        t.spans.append(Span {0, int(t.text.size()), {}, -1});
        return t;
    }();
    return summary;
}

// The parser already caps nesting (Converter::MaxDepth, model/converter.h);
// this is a second bound so layout recursion stays safe on its own.
constexpr int MaxNesting = 100;

// A document this long (in source lines) is laid out in parts on several
// threads; for shorter ones starting the threads costs more than it saves.
constexpr int ParallelMinLines = 2000;
// Qt serialises font lookups behind one lock, so more threads gain nothing:
// on a 6.5 MB manual 8 threads were 3.7 times faster than one, 12 no faster.
constexpr int MaxLayoutThreads = 8;

using BlockList = std::vector<std::unique_ptr<Block>>;

// Where each part of a parallel layout starts: indices into the top-level
// blocks, splitting the source into runs of about equal length. Just {0}
// when the document is laid out in one piece.
std::vector<size_t> partStarts(const BlockList &blocks, int threads)
{
    std::vector<size_t> starts {0};
    int lines = 0;
    for (const auto &b : blocks)
        lines = qMax(lines, b->sourceLine);
    if (lines < ParallelMinLines || threads < 2)
        return starts;
    const qint64 parts = qMin<qint64>(threads, qint64(blocks.size()));
    int seen = 0; // footnotes come last whatever their line, so take the highest yet
    for (size_t i = 1; i < blocks.size() && qint64(starts.size()) < parts; ++i) {
        seen = qMax(seen, blocks[i]->sourceLine);
        if (seen >= lines * qint64(starts.size()) / parts)
            starts.push_back(i);
    }
    return starts;
}

// The builder's questions about images, answered ahead of time on the UI
// thread for the threads that build parts of a layout: ImageLoader lives on
// the UI thread and must not be called from others.
class ImageAnswers : public ImageSource
{
public:
    void ask(ImageSource &source, const QString &src)
    {
        if (m_answers.contains(src))
            return;
        Answer a;
        a.state = source.request(src);
        if (a.state == Ready)
            a.size = source.naturalSize(src);
        m_answers.insert(src, a);
    }

    // Only reads, so several threads may call these at once.
    State request(const QString &src) override { return m_answers.value(src).state; }
    QSizeF naturalSize(const QString &src) override { return m_answers.value(src).size; }
    void paint(QPainter &, const QString &, const QRectF &, bool) override { } // the builder never paints

private:
    struct Answer {
        State state = Failed;
        QSizeF size;
    };
    QHash<QString, Answer> m_answers;
};

} // namespace

class LayoutBuilder
{
public:
    LayoutBuilder(Layout &layout, const LayoutOptions &options, CodeHighlighter *highlighter, ImageSource *images)
        : m_layout(layout)
        , m_theme(layout.m_theme)
        , m_options(options)
        , m_highlighter(highlighter)
        , m_images(images)
    { }

    void run(const Document &doc, std::unique_ptr<Layout> previous)
    {
        const int threads =
            m_options.maxThreads > 0 ? m_options.maxThreads : qMin(QThread::idealThreadCount(), MaxLayoutThreads);
        const std::vector<size_t> starts = partStarts(doc.blocks, threads);
        const auto partEnd = [&](size_t k) { return k + 1 < starts.size() ? starts[k + 1] : doc.blocks.size(); };

        // The earlier layout's boxes, split the same way.
        std::vector<std::vector<std::unique_ptr<TextBox>>> reusable(starts.size());
        m_layout.m_doc = &doc;
        if (previous && previous->m_doc == &doc && previous->m_topBoxStart.size() == doc.blocks.size()) {
            auto &old = previous->texts;
            const auto boxStart = [&](size_t block) {
                return qsizetype(block < doc.blocks.size() ? previous->m_topBoxStart[block] : old.size());
            };
            for (size_t k = 0; k < starts.size(); ++k) {
                reusable[k].assign(std::make_move_iterator(old.begin() + boxStart(starts[k])),
                                   std::make_move_iterator(old.begin() + boxStart(partEnd(k))));
            }
        }
        m_layout.m_width = m_options.width;

        if (starts.size() == 1) {
            m_reusable = std::move(reusable[0]);
            topLevel(doc.blocks, 0, doc.blocks.size());
            m_layout.m_height = m_y;
            return;
        }

        ImageAnswers answers;
        prepare(doc.blocks, 0, answers);
        std::vector<std::unique_ptr<Layout>> parts(starts.size());
        const auto buildPart = [&](size_t k) {
            LayoutBuilder part(*parts[k], m_options, m_highlighter, m_images ? &answers : nullptr);
            part.m_onWorker = true;
            part.m_reusable = std::move(reusable[k]);
            part.topLevel(doc.blocks, starts[k], partEnd(k));
            parts[k]->m_height = part.m_y;
        };
        std::vector<std::thread> workers;
        for (size_t k = 0; k < starts.size(); ++k) {
            parts[k].reset(new Layout(m_theme));
            try {
                workers.emplace_back(buildPart, k);
            } catch (const std::system_error &) {
                buildPart(k); // no thread to be had (a process or memory limit): build it here
            }
        }
        for (std::thread &t : workers)
            t.join();

        size_t boxes = 0;
        for (const auto &part : parts)
            boxes += part->texts.size();
        m_layout.texts.reserve(boxes);
        for (size_t k = 0; k < parts.size(); ++k) {
            if (k > 0)
                m_y += gap(*doc.blocks[starts[k] - 1], *doc.blocks[starts[k]], topCtx());
            append(*parts[k], m_y);
            m_y += parts[k]->m_height;
        }
        m_layout.m_height = m_y;
    }

private:
    struct Ctx {
        qreal x = 0;
        qreal width = 0;
        bool muted = false;
        bool small = false;
        bool tight = false;
        int listDepth = 0;
        int depth = 0; // nesting of containers; see blocks()
    };

    // The context of the top-level blocks, as blocks() makes it for them.
    Ctx topCtx() const
    {
        Ctx ctx;
        ctx.width = m_options.width;
        ctx.depth = 1;
        return ctx;
    }

    // Lays out top-level blocks [begin, end), as blocks() would, noting where
    // each one's boxes start.
    void topLevel(const BlockList &list, size_t begin, size_t end)
    {
        m_layout.texts.reserve(m_reusable.size());
        const Ctx ctx = topCtx();
        for (size_t i = begin; i < end; ++i) {
            if (i > begin)
                m_y += gap(*list[i - 1], *list[i], ctx);
            m_layout.m_topBoxStart.push_back(m_layout.texts.size());
            block(*list[i], ctx);
        }
    }

    // Done on the UI thread before parts are built on others: highlights the
    // code blocks and asks about the images that the layout will show, the
    // two things the builder needs that may not be used from other threads.
    // Visits exactly the blocks that blocks() lays out.
    void prepare(const BlockList &list, int depth, ImageAnswers &answers)
    {
        if (depth >= MaxNesting)
            return;
        ++depth;
        for (const auto &b : list) {
            switch (b->type) {
            case Block::Paragraph:
            case Block::Heading:
                prepareInline(b->inl, answers);
                break;
            case Block::CodeBlock:
                if (m_highlighter)
                    m_highlighter->highlight(*b);
                break;
            case Block::Table:
                for (const QList<InlineText> &row : std::as_const(b->rows)) {
                    for (const InlineText &cell : row)
                        prepareInline(cell, answers);
                }
                break;
            case Block::List:
                for (const auto &item : b->children)
                    prepare(item->children, depth, answers);
                break;
            case Block::Details:
                prepareInline(b->inl, answers);
                if (detailsOpen(*b))
                    prepare(b->children, depth, answers);
                break;
            default:
                prepare(b->children, depth, answers);
                break;
            }
        }
    }

    void prepareInline(const InlineText &inl, ImageAnswers &answers)
    {
        if (!m_images)
            return;
        for (const InlineImage &img : inl.images)
            answers.ask(*m_images, img.src);
    }

    // Moves a part built on another thread into this layout, `dy` lower.
    void append(Layout &part, qreal dy)
    {
        const int frameBase = int(m_layout.frames.size());
        const size_t boxBase = m_layout.texts.size();
        const auto frame = [frameBase](int f) { return f >= 0 ? f + frameBase : f; };
        for (auto &box : part.texts) {
            box->pos.ry() += dy;
            box->frame = frame(box->frame);
            // A QTextLayout keeps the font engine it last used, and engines
            // belong to the thread that loaded them: drop it, so painting
            // here loads this thread's own.
            box->layout.setFont(box->layout.font());
            m_layout.texts.push_back(std::move(box));
        }
        for (Deco &d : part.decos) {
            d.rect.translate(0, dy);
            d.frame = frame(d.frame);
            m_layout.decos.push_back(std::move(d));
        }
        for (ScrollFrame &f : part.frames) {
            f.rect.translate(0, dy);
            m_layout.frames.push_back(f);
        }
        for (CodeArea &a : part.codeAreas) {
            a.rect.translate(0, dy);
            m_layout.codeAreas.push_back(a);
        }
        for (ToggleArea &t : part.toggles) {
            t.rect.translate(0, dy);
            m_layout.toggles.push_back(t);
        }
        for (const auto &[top, bottom] : std::as_const(part.unbreakable))
            m_layout.unbreakable.append({top + dy, bottom + dy});
        for (auto it = part.m_blockTops.cbegin(); it != part.m_blockTops.cend(); ++it)
            m_layout.m_blockTops.insert(it.key(), it.value() + dy);
        for (const auto &[top, block] : std::as_const(part.m_blockOrder))
            m_layout.m_blockOrder.append({top + dy, block});
        for (size_t start : part.m_topBoxStart)
            m_layout.m_topBoxStart.push_back(start + boxBase);
    }

    bool detailsOpen(const Block &b) const
    {
        const bool toggled = m_options.toggledDetails && m_options.toggledDetails->contains(&b);
        return m_options.expandAllDetails || b.open != toggled;
    }

    struct Style {
        QFont font;
        QColor color;
        qreal lineHeight = 0;
        Qt::Alignment align = Qt::AlignLeft;
    };

    qreal em(qreal f) const { return m_theme.px * f; }

    Style bodyStyle(const Ctx &ctx, Align align = Align::Default) const
    {
        Style s;
        s.font = m_theme.body;
        qreal px = m_theme.px;
        if (ctx.small) {
            px *= Metrics::SmallText;
            s.font.setPixelSize(qRound(px));
        }
        s.color = ctx.muted ? m_theme.muted : m_theme.text;
        s.lineHeight = std::round(px * Metrics::BodyLineHeight);
        s.align = toQt(align);
        return s;
    }

    void recordTop(const Block &b)
    {
        m_layout.m_blockTops.insert(&b, m_y);
        m_layout.m_blockOrder.append({m_y, &b});
    }

    QSizeF imageSize(const InlineImage &img, bool *ready) const
    {
        QSizeF natural;
        *ready = m_images && m_images->request(img.src) == ImageSource::Ready;
        if (*ready)
            natural = m_images->naturalSize(img.src);
        if (img.width > 0 && img.height > 0)
            return QSizeF(img.width, img.height);
        if (*ready && natural.width() > 0 && natural.height() > 0) {
            if (img.width > 0)
                return QSizeF(img.width, img.width * natural.height() / natural.width());
            if (img.height > 0)
                return QSizeF(img.height * natural.width() / natural.height(), img.height);
            return natural;
        }
        // Not loaded: a chip showing the alt text.
        const QFontMetricsF fm(placeholderFont(m_theme));
        const QString label = placeholderLabel(img);
        const qreal w =
            qMin(fm.horizontalAdvance(label) + em(Metrics::PlaceholderPadX), em(Metrics::PlaceholderMaxWidth));
        return QSizeF(img.width > 0 ? img.width : w,
                      img.height > 0 ? img.height : fm.height() + em(Metrics::PlaceholderPadY));
    }

    // The text font with a span's formatting applied.
    QFont spanFont(const Span &span, const QFont &base) const
    {
        QFont f = base;
        if (span.flags & Span::Bold)
            f.setWeight(QFont::DemiBold);
        if (span.flags & Span::Italic)
            f.setItalic(true);
        if (span.flags & Span::Strike)
            f.setStrikeOut(true);
        if (span.flags & (Span::Code | Span::Kbd)) {
            f.setFamilies(m_theme.mono.families());
            f.setStyleHint(QFont::Monospace);
            f.setPixelSize(qMax(Metrics::MinInlineCodePx, qRound(base.pixelSize() * Metrics::SmallText)));
        }
        return f;
    }

    // The earlier layout's next box, if it is the one about to be built: with
    // only the width changed, boxes come in the same order. Boxes with
    // images are built again, as their images may have loaded since.
    std::unique_ptr<TextBox> reuse(const Block *block, const InlineText *inl)
    {
        if (m_nextReusable >= m_reusable.size())
            return nullptr;
        std::unique_ptr<TextBox> &box = m_reusable[m_nextReusable];
        if (box->block != block || box->inl != inl) {
            m_nextReusable = m_reusable.size(); // out of step: build the rest afresh
            return nullptr;
        }
        ++m_nextReusable;
        if (!box->images.isEmpty())
            return nullptr;
        box->frame = -1;
        box->separator = QStringLiteral("\n"); // as for a new box; callers set the rest
        return std::move(box);
    }

    std::unique_ptr<TextBox> makeBox(const InlineText &inl, const Style &style, const Block *block)
    {
        if (auto reused = reuse(block, &inl))
            return reused;
        auto box = std::make_unique<TextBox>();
        box->block = block;
        box->inl = &inl;
        box->layout.setText(inl.text);
        box->layout.setFont(style.font);
        box->m_lineHeight = style.lineHeight;
        box->m_align = style.align;

        QList<QTextLayout::FormatRange> formats;
        QTextLayout::FormatRange all;
        all.start = 0;
        all.length = inl.text.size();
        all.format.setForeground(style.color);
        formats.append(all);

        for (const Span &span : inl.spans) {
            if (!span.flags && span.link < 0)
                continue;
            QTextLayout::FormatRange r;
            r.start = span.start;
            r.length = span.length;
            r.format.setFont(spanFont(span, style.font));
            r.format.setForeground(span.link >= 0 ? m_theme.link : style.color);
            if (span.flags & Span::Sub)
                r.format.setVerticalAlignment(QTextCharFormat::AlignSubScript);
            else if (span.flags & Span::Sup)
                r.format.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
            formats.append(r);
        }
        box->m_baseFormats = formats;
        box->layout.setFormats(formats);

        for (const InlineImage &img : inl.images) {
            ImageSlot slot;
            slot.image = &img;
            slot.natural = imageSize(img, &slot.ready);
            box->images.append(slot);
        }
        return box;
    }

    TextBox *place(std::unique_ptr<TextBox> box, qreal x, qreal y)
    {
        box->pos = QPointF(x, y);
        TextBox *raw = box.get();
        m_layout.texts.push_back(std::move(box));
        return raw;
    }

    qreal gap(const Block &prev, const Block &cur, const Ctx &ctx) const
    {
        if (cur.type == Block::Heading || cur.type == Block::Rule || prev.type == Block::Rule)
            return em(Metrics::SectionGap);
        if (cur.type == Block::Footnote && prev.type == Block::Footnote)
            return em(Metrics::FootnoteGap);
        if (ctx.tight)
            return em(Metrics::TightGap);
        return em(Metrics::BlockGap);
    }

    void blocks(const BlockList &list, const Ctx &parent)
    {
        if (parent.depth >= MaxNesting)
            return;
        Ctx ctx = parent;
        ++ctx.depth;
        const Block *prev = nullptr;
        for (const auto &b : list) {
            if (prev)
                m_y += gap(*prev, *b, ctx);
            block(*b, ctx);
            prev = b.get();
        }
    }

    void block(const Block &b, const Ctx &ctx)
    {
        recordTop(b);
        switch (b.type) {
        case Block::Paragraph:
            paragraph(b, ctx);
            break;
        case Block::Heading:
            heading(b, ctx);
            break;
        case Block::CodeBlock:
            code(b, ctx);
            break;
        case Block::Quote:
            quote(b, ctx);
            break;
        case Block::List:
            list(b, ctx);
            break;
        case Block::ListItem:
            blocks(b.children, ctx);
            break;
        case Block::Table:
            table(b, ctx);
            break;
        case Block::Rule:
            m_layout.decos.push_back(
                makeDeco(QRectF(ctx.x, m_y, ctx.width, em(Metrics::RuleThickness)), m_theme.border));
            m_y += em(Metrics::RuleThickness);
            break;
        case Block::Details:
            details(b, ctx);
            break;
        case Block::Footnote:
            footnote(b, ctx);
            break;
        }
    }

    static Deco makeDeco(const QRectF &rect, const QColor &color, Deco::Kind kind = Deco::Fill)
    {
        Deco d;
        d.kind = kind;
        d.rect = rect;
        d.color = color;
        return d;
    }

    void paragraph(const Block &b, const Ctx &ctx)
    {
        auto box = makeBox(b.inl, bodyStyle(ctx, b.align), &b);
        box->doLayout(ctx.width, QTextOption::WrapAtWordBoundaryOrAnywhere);
        box->separator = ctx.tight ? QStringLiteral("\n") : QStringLiteral("\n\n");
        const qreal h = box->height;
        place(std::move(box), ctx.x, m_y);
        m_y += h;
    }

    void heading(const Block &b, const Ctx &ctx)
    {
        const int level = qBound(1, b.level, 6);
        const qreal px = m_theme.px * Metrics::HeadingScale[level - 1] * (ctx.small ? Metrics::SmallText : 1.0);

        Style s;
        s.font = m_theme.body;
        s.font.setPixelSize(qRound(px));
        s.font.setWeight(QFont::DemiBold);
        s.color = (level == 6 || ctx.muted) ? m_theme.muted : m_theme.text;
        s.lineHeight = std::round(px * Metrics::HeadingLineHeight);
        s.align = toQt(b.align);

        const qreal top = m_y;
        auto box = makeBox(b.inl, s, &b);
        box->doLayout(ctx.width, QTextOption::WrapAtWordBoundaryOrAnywhere);
        box->separator = QStringLiteral("\n\n");
        const qreal h = box->height;
        place(std::move(box), ctx.x, m_y);
        m_y += h;
        if (level <= 2) {
            m_y += px * Metrics::HeadingRuleGap;
            m_layout.decos.push_back(makeDeco(QRectF(ctx.x, m_y, ctx.width, Metrics::HairlinePx), m_theme.border));
            m_y += Metrics::HairlinePx;
        }
        // Keep a heading on the same page as the first lines after it.
        m_layout.unbreakable.append({top, m_y + em(Metrics::KeepWithHeading)});
    }

    void code(const Block &b, const Ctx &ctx)
    {
        const qreal pad = em(Metrics::CodePadding);
        const qreal innerWidth = qMax(em(Metrics::MinColumn), ctx.width - 2 * pad);
        const qreal top = m_y;

        QFont font = m_theme.mono;
        const qreal lineHeight = std::round(font.pixelSize() * Metrics::CodeLineHeight);
        const qreal tabStop = QFontMetricsF(font).horizontalAdvance(u' ') * Metrics::TabWidthSpaces;
        // On a worker, prepare() has highlighted everything already.
        const HighlightedLines *highlighted = !m_highlighter ? nullptr
            : m_onWorker                                     ? m_highlighter->cached(b)
                                                             : m_highlighter->highlight(b);
        const QColor color = ctx.muted ? m_theme.muted : m_theme.text;

        const size_t decoIndex = m_layout.decos.size();
        m_layout.decos.push_back(makeDeco(QRectF(), m_theme.codeBackground, Deco::RoundFill));

        const size_t firstBox = m_layout.texts.size();
        qreal y = top + pad;
        qreal widest = 0;
        const auto mode = m_options.wrapCode ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap;

        const QList<QStringView> lines = codeLines(b.code);
        for (int lineNo = 0; lineNo < lines.size(); ++lineNo) {
            const QStringView line = lines[lineNo];
            std::unique_ptr<TextBox> box = reuse(&b, nullptr);
            if (!box) {
                box = std::make_unique<TextBox>();
                box->block = &b;
                box->isCode = true;
                box->layout.setText(line.toString());
                box->layout.setFont(font);
                box->m_lineHeight = lineHeight;
                box->m_tabStop = tabStop;
                const QList<HighlightRange> *ranges =
                    highlighted && lineNo < highlighted->size() ? &highlighted->at(lineNo) : nullptr;
                box->layout.setFormats(codeLineFormats(int(line.size()), ranges, font, color));
            }
            box->doLayout(innerWidth, mode);
            widest = qMax(widest, box->naturalWidth);
            const qreal h = box->height;
            place(std::move(box), ctx.x + pad, y);
            y += h;
        }
        m_layout.texts.back()->separator = QStringLiteral("\n\n");

        m_y = y + pad;
        const QRectF area(ctx.x, top, ctx.width, m_y - top);
        m_layout.decos[decoIndex].rect = area;
        m_layout.codeAreas.push_back(CodeArea {area, &b});

        if (!m_options.wrapCode && widest > innerWidth + Metrics::ScrollSlackPx) {
            ScrollFrame frame;
            frame.rect = area;
            frame.contentWidth = widest + 2 * pad;
            frame.block = &b;
            m_layout.frames.push_back(frame);
            for (size_t i = firstBox; i < m_layout.texts.size(); ++i)
                m_layout.texts[i]->frame = int(m_layout.frames.size() - 1);
        }
    }

    // The formats of one code line: its text colour, then the highlighter's
    // ranges on top (null when the language is unknown or the line is past
    // what was highlighted).
    static QList<QTextLayout::FormatRange> codeLineFormats(int length, const QList<HighlightRange> *ranges,
                                                           const QFont &font, const QColor &color)
    {
        QList<QTextLayout::FormatRange> formats;
        QTextLayout::FormatRange all;
        all.start = 0;
        all.length = length;
        all.format.setForeground(color);
        formats.append(all);
        if (!ranges)
            return formats;
        for (const HighlightRange &h : *ranges) {
            QTextLayout::FormatRange r;
            r.start = h.start;
            r.length = h.length;
            QFont f = font;
            f.setBold(h.bold);
            f.setItalic(h.italic);
            f.setUnderline(h.underline);
            r.format.setFont(f);
            r.format.setForeground(h.hasColor ? QColor::fromRgb(h.color) : color);
            formats.append(r);
        }
        return formats;
    }

    void quote(const Block &b, const Ctx &ctx)
    {
        Ctx inner = ctx;
        inner.x = ctx.x + em(Metrics::QuoteIndent);
        inner.width = qMax(em(Metrics::MinNestedColumn), ctx.width - em(Metrics::QuoteIndent));
        inner.muted = true;
        const qreal top = m_y;
        blocks(b.children, inner);
        if (m_y > top)
            m_layout.decos.push_back(
                makeDeco(QRectF(ctx.x, top, em(Metrics::QuoteBarWidth), m_y - top), m_theme.border));
    }

    void list(const Block &b, const Ctx &ctx)
    {
        const qreal indent = em(Metrics::ListIndent);
        Ctx inner = ctx;
        inner.x = ctx.x + indent;
        inner.width = qMax(em(Metrics::MinNestedColumn), ctx.width - indent);
        inner.tight = b.tight;
        inner.listDepth = ctx.listDepth + 1;

        const Style style = bodyStyle(ctx);
        int number = b.listStart;
        bool first = true;
        for (const auto &item : b.children) {
            if (!first)
                m_y += b.tight ? em(Metrics::TightGap) : em(Metrics::BlockGap);
            first = false;
            recordTop(*item);
            const qreal top = m_y;

            Deco marker;
            marker.color = style.color;
            if (item->task != Task::None) {
                const qreal s = em(Metrics::CheckboxSize);
                marker.kind = Deco::Checkbox;
                marker.on = item->task == Task::Done;
                marker.rect = QRectF(inner.x - em(Metrics::CheckboxOffset), top + (style.lineHeight - s) / 2, s, s);
            } else if (b.ordered) {
                marker.kind = Deco::Text;
                marker.text = QString::number(number) + u'.';
                marker.font = style.font;
                marker.rect = QRectF(ctx.x - indent, top, 2 * indent - em(Metrics::ListNumberGap), style.lineHeight);
            } else {
                const qreal s = em(Metrics::BulletSize);
                marker.kind = ctx.listDepth == 0 ? Deco::Disc : ctx.listDepth == 1 ? Deco::Circle : Deco::Square;
                marker.rect = QRectF(inner.x - em(Metrics::BulletOffset), top + (style.lineHeight - s) / 2, s, s);
            }
            m_layout.decos.push_back(marker);
            ++number;

            blocks(item->children, inner);
            if (m_y == top)
                m_y += style.lineHeight;
        }
    }

    // Tables follow the browser's automatic layout: each cell is measured
    // unwrapped (its widest) and wrapped as tightly as possible (its
    // narrowest), then columns share the available width between the two. A
    // table that cannot fit scrolls sideways in its own frame.
    void table(const Block &b, const Ctx &ctx)
    {
        if (b.columns.isEmpty() || b.rows.isEmpty())
            return;

        TableGeometry g;
        g.x = ctx.x;
        g.top = m_y;
        g.padX = em(Metrics::TableCellPadX);
        g.padY = em(Metrics::TableCellPadY);
        TableCells cells = measureTableCells(b, ctx);

        const int cols = int(b.columns.size());
        const qreal chrome = cols * 2 * g.padX + (cols + 1) * g.border;
        g.columnWidths = shareColumnWidths(cells.minWidth, cells.maxWidth, ctx.width - chrome);
        g.width = chrome;
        for (qreal w : std::as_const(g.columnWidths))
            g.width += w;
        if (g.width > ctx.width + Metrics::ScrollSlackPx) {
            m_layout.frames.emplace_back();
            g.frame = int(m_layout.frames.size() - 1);
        }

        const QList<qreal> rowTops = placeTableRows(cells, g);
        m_y += g.border;
        drawTableGrid(g, rowTops);

        if (g.frame >= 0) {
            m_y += em(Metrics::TableScrollBarRoom);
            ScrollFrame &frame = m_layout.frames[g.frame];
            frame.rect = QRectF(ctx.x, g.top, ctx.width, m_y - g.top);
            frame.contentWidth = g.width;
            frame.block = &b;
        }
    }

    struct TableCells {
        std::vector<std::vector<std::unique_ptr<TextBox>>> boxes; // [row][column]
        QList<qreal> minWidth;                                    // per column: the narrowest any cell can wrap to
        QList<qreal> maxWidth;                                    // per column: the widest any cell is unwrapped
    };

    struct TableGeometry {
        qreal x = 0;
        qreal top = 0;
        qreal width = 0; // including padding and borders
        qreal padX = 0;
        qreal padY = 0;
        qreal border = Metrics::HairlinePx;
        QList<qreal> columnWidths; // text width of each column
        int frame = -1;            // scroll frame, if the table is wider than the column
    };

    TableCells measureTableCells(const Block &b, const Ctx &ctx)
    {
        const int rows = int(b.rows.size());
        const int cols = int(b.columns.size());
        TableCells cells;
        cells.boxes.resize(rows);
        cells.minWidth.fill(0, cols);
        cells.maxWidth.fill(0, cols);
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                Style s = bodyStyle(ctx);
                if (r == 0)
                    s.font.setWeight(QFont::DemiBold);
                s.align = toQt(b.columns[c], r == 0 ? Qt::AlignHCenter : Qt::AlignLeft);
                auto box = makeBox(b.rows[r][c], s, &b);
                if (box->m_widest < 0) {
                    box->doLayout(Unbounded, QTextOption::NoWrap, ctx.width);
                    box->m_widest = std::ceil(box->naturalWidth) + Metrics::CellSlackPx;
                    box->doLayout(1, QTextOption::WordWrap, ctx.width);
                    box->m_narrowest = qMin(box->m_widest, std::ceil(box->naturalWidth) + Metrics::CellSlackPx);
                }
                cells.maxWidth[c] = qMax(cells.maxWidth[c], box->m_widest);
                cells.minWidth[c] = qMax(cells.minWidth[c], box->m_narrowest);
                cells.boxes[r].push_back(std::move(box));
            }
        }
        return cells;
    }

    // Lays out and places every cell, row by row; returns the top of each row.
    QList<qreal> placeTableRows(TableCells &cells, const TableGeometry &g)
    {
        const int rows = int(cells.boxes.size());
        const int cols = int(g.columnWidths.size());
        QList<qreal> rowTops;
        for (int r = 0; r < rows; ++r) {
            qreal tallest = 0;
            for (int c = 0; c < cols; ++c) {
                cells.boxes[r][c]->doLayout(g.columnWidths[c], QTextOption::WrapAtWordBoundaryOrAnywhere);
                tallest = qMax(tallest, cells.boxes[r][c]->height);
            }
            const qreal rowHeight = tallest + 2 * g.padY + g.border;
            rowTops.append(m_y);
            if (r > 0 && r % 2 == 0) { // striped body rows, as on GitHub
                Deco stripe = makeDeco(QRectF(g.x, m_y, g.width, rowHeight), m_theme.tableAltBackground);
                stripe.frame = g.frame;
                m_layout.decos.push_back(stripe);
            }
            qreal x = g.x + g.border;
            for (int c = 0; c < cols; ++c) {
                auto &box = cells.boxes[r][c];
                box->frame = g.frame;
                // Copying a table gives tab-separated columns and one line per row.
                box->separator = c + 1 < cols ? QStringLiteral("\t")
                    : r + 1 < rows            ? QStringLiteral("\n")
                                              : QStringLiteral("\n\n");
                const qreal offset = g.border + g.padY + (tallest - box->height) / 2;
                place(std::move(box), x + g.padX, m_y + offset);
                x += g.columnWidths[c] + 2 * g.padX + g.border;
            }
            m_layout.unbreakable.append({m_y, m_y + rowHeight});
            m_y += rowHeight;
        }
        return rowTops;
    }

    void drawTableGrid(const TableGeometry &g, const QList<qreal> &rowTops)
    {
        auto line = [&](const QRectF &rect) {
            Deco d = makeDeco(rect, m_theme.border);
            d.frame = g.frame;
            m_layout.decos.push_back(d);
        };
        for (qreal rowTop : rowTops)
            line(QRectF(g.x, rowTop, g.width, g.border));
        line(QRectF(g.x, m_y - g.border, g.width, g.border));
        qreal x = g.x;
        for (int c = 0; c <= g.columnWidths.size(); ++c) {
            line(QRectF(x, g.top, g.border, m_y - g.top));
            if (c < g.columnWidths.size())
                x += g.columnWidths[c] + 2 * g.padX + g.border;
        }
    }

    void details(const Block &b, const Ctx &ctx)
    {
        const bool open = detailsOpen(b);
        const Style style = bodyStyle(ctx);
        const qreal top = m_y;

        Deco arrow;
        arrow.kind = Deco::Triangle;
        arrow.on = open;
        arrow.color = style.color;
        const qreal s = em(Metrics::TriangleSize);
        arrow.rect = QRectF(ctx.x + em(Metrics::TriangleInset), top + (style.lineHeight - s) / 2, s, s);
        m_layout.decos.push_back(arrow);

        auto box = makeBox(b.inl.isEmpty() ? defaultSummary() : b.inl, style, &b);
        box->doLayout(qMax(em(Metrics::MinColumn), ctx.width - em(Metrics::SummaryIndent)),
                      QTextOption::WrapAtWordBoundaryOrAnywhere);
        const qreal h = box->height;
        place(std::move(box), ctx.x + em(Metrics::SummaryIndent), m_y);
        m_y += h;
        m_layout.toggles.push_back(ToggleArea {QRectF(ctx.x, top, ctx.width, h), &b});

        if (open && !b.children.empty()) {
            m_y += em(Metrics::DetailsGap);
            blocks(b.children, ctx);
        }
    }

    void footnote(const Block &b, const Ctx &ctx)
    {
        if (b.number == 1) {
            m_layout.decos.push_back(makeDeco(QRectF(ctx.x, m_y, ctx.width, Metrics::HairlinePx), m_theme.border));
            m_y += em(Metrics::BlockGap);
            // The block was recorded above the separator; it starts below it.
            m_layout.m_blockTops.insert(&b, m_y);
            m_layout.m_blockOrder.last().first = m_y;
        }
        Ctx inner = ctx;
        inner.x = ctx.x + em(Metrics::FootnoteIndent);
        inner.width = qMax(em(Metrics::MinNestedColumn), ctx.width - em(Metrics::FootnoteIndent));
        inner.small = true;
        inner.muted = true;
        const Style style = bodyStyle(inner);

        Deco marker;
        marker.kind = Deco::Text;
        marker.text = QString::number(b.number) + u'.';
        marker.font = style.font;
        marker.color = style.color;
        marker.rect = QRectF(ctx.x, m_y, em(Metrics::FootnoteIndent - Metrics::ListNumberGap), style.lineHeight);
        m_layout.decos.push_back(marker);

        const qreal top = m_y;
        blocks(b.children, inner);
        if (m_y == top)
            m_y += style.lineHeight;
    }

    Layout &m_layout;
    const Theme &m_theme;
    const LayoutOptions &m_options;
    CodeHighlighter *m_highlighter;
    ImageSource *m_images;
    std::vector<std::unique_ptr<TextBox>> m_reusable;
    size_t m_nextReusable = 0;
    bool m_onWorker = false; // building one part of a parallel layout
    qreal m_y = 0;
};

void buildLayout(Layout &layout, const Document &doc, const LayoutOptions &options, CodeHighlighter *highlighter,
                 ImageSource *images, std::unique_ptr<Layout> previous)
{
    LayoutBuilder(layout, options, highlighter, images).run(doc, std::move(previous));
}

} // namespace md
