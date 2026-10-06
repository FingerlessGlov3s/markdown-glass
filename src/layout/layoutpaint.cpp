#include "layout/layoutinternal.h"

// Painting a Layout: decorations first, then text boxes, each clipped and
// shifted by its scroll frame if it has one.

#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace md {

namespace {

// A task list's check box, as fractions of its size: the corner radius, the
// tick's stroke, and the three points of the tick.
constexpr qreal CheckboxRadius = 0.2;
constexpr qreal CheckboxStroke = 0.14;
constexpr qreal MinCheckboxStroke = 1.5; // pixels
constexpr QPointF TickStart(0.24, 0.52);
constexpr QPointF TickCorner(0.43, 0.70);
constexpr QPointF TickEnd(0.77, 0.30);

// Rounded backgrounds behind inline code and keyboard keys.
void paintInlineBackgrounds(QPainter &p, const TextBox &b, const Theme &theme)
{
    if (!b.inl)
        return;
    const qreal radius = theme.em(Metrics::InlineRadius);
    for (const Span &span : b.inl->spans) {
        if (!(span.flags & (Span::Code | Span::Kbd)))
            continue;
        const bool kbd = span.flags & Span::Kbd;
        const qreal out = theme.em(kbd ? Metrics::KbdOverhang : Metrics::InlineCodeOverhang);
        const auto rects = b.rangeRects(span.start, span.length, LineBox::Glyphs);
        for (const QRectF &local : rects) {
            const QRectF r = local.translated(b.pos).adjusted(-out, 0, out, 0);
            p.setPen(kbd ? QPen(theme.border, 1) : QPen(Qt::NoPen));
            p.setBrush(kbd ? theme.codeBackground : theme.inlineCodeBackground);
            p.drawRoundedRect(r, radius, radius);
            if (kbd)
                p.fillRect(QRectF(r.left() + radius, r.bottom(), r.width() - 2 * radius, 1), theme.border);
        }
    }
}

void paintFindMatches(QPainter &p, const TextBox &b, int index, const PaintState &state, const Theme &theme)
{
    if (!state.matches || state.matches->empty())
        return;
    auto it = std::lower_bound(state.matches->begin(), state.matches->end(), index,
                               [](const FindMatch &m, int box) { return m.box < box; });
    for (; it != state.matches->end() && it->box == index; ++it) {
        const bool current = int(it - state.matches->begin()) == state.currentMatch;
        const auto rects = b.rangeRects(it->start, it->length, LineBox::Glyphs);
        for (const QRectF &local : rects)
            p.fillRect(local.translated(b.pos), current ? theme.findCurrent : theme.findMatch);
    }
}

// Fills the selected part of the box and adds the selected-text colour to `overlays`.
void paintSelection(QPainter &p, const TextBox &b, int index, const PaintState &state, const Theme &theme,
                    QList<QTextLayout::FormatRange> &overlays)
{
    if (state.selStartBox < 0 || index < state.selStartBox || index > state.selEndBox)
        return;
    const int length = b.layout.text().size();
    const int from = index == state.selStartBox ? state.selStartPos : 0;
    const int to = index == state.selEndBox ? state.selEndPos : length;
    if (to > from) {
        const auto rects = b.rangeRects(from, to - from, LineBox::FullLine);
        for (const QRectF &local : rects)
            p.fillRect(local.translated(b.pos), theme.selection);
        QTextLayout::FormatRange r;
        r.start = from;
        r.length = to - from;
        r.format.setForeground(theme.selectionText);
        overlays.append(r);
    } else if (length == 0 && index < state.selEndBox) {
        // An empty line inside the selection still shows a sliver.
        p.fillRect(QRectF(b.pos, QSizeF(theme.em(Metrics::EmptyLineSelection), b.height)), theme.selection);
    }
}

void underlineHoveredLink(const TextBox &b, int index, const PaintState &state,
                          QList<QTextLayout::FormatRange> &overlays)
{
    if (state.hoverBox != index || state.hoverLink < 0 || !b.inl)
        return;
    for (const Span &span : b.inl->spans) {
        if (span.link != state.hoverLink)
            continue;
        QTextLayout::FormatRange r;
        r.start = span.start;
        r.length = span.length;
        r.format.setFontUnderline(true);
        overlays.append(r);
    }
}

// Each image, or while it is not available a chip with its alt text.
void paintImages(QPainter &p, const TextBox &b, const PaintState &state, const Theme &theme)
{
    const qreal radius = theme.em(Metrics::InlineRadius);
    for (const ImageSlot &slot : b.images) {
        const QRectF target = slot.rect.translated(b.pos);
        const QString &src = slot.image->src;
        if (slot.ready && state.images && state.images->request(src) == ImageSource::Ready) {
            state.images->paint(p, src, target, state.preview);
            continue;
        }
        p.setPen(QPen(theme.border, 1));
        p.setBrush(theme.codeBackground);
        p.drawRoundedRect(target.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
        const QFont small = placeholderFont(theme);
        p.setFont(small);
        p.setPen(theme.muted);
        const QString label = placeholderLabel(*slot.image);
        const qreal inset = theme.em(Metrics::PlaceholderTextInset);
        const QRectF inner = target.adjusted(inset, 0, -inset, 0);
        p.drawText(inner, Qt::AlignCenter, QFontMetricsF(small).elidedText(label, Qt::ElideRight, inner.width()));
    }
}

} // namespace

void Layout::paintDeco(QPainter &p, const Deco &d) const
{
    switch (d.kind) {
    case Deco::Fill:
        p.fillRect(d.rect, d.color);
        break;
    case Deco::RoundFill: {
        QPainterPath path;
        path.addRoundedRect(d.rect, m_theme.em(Metrics::CodeBlockRadius), m_theme.em(Metrics::CodeBlockRadius));
        p.fillPath(path, d.color);
        break;
    }
    case Deco::Disc:
        p.setPen(Qt::NoPen);
        p.setBrush(d.color);
        p.drawEllipse(d.rect);
        break;
    case Deco::Circle:
        p.setPen(QPen(d.color, 1));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(d.rect);
        break;
    case Deco::Square:
        p.fillRect(d.rect, d.color);
        break;
    case Deco::Checkbox: {
        const qreal radius = d.rect.width() * CheckboxRadius;
        if (d.on) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_theme.link);
            p.drawRoundedRect(d.rect, radius, radius);
            QPen pen(m_theme.background, qMax(MinCheckboxStroke, d.rect.width() * CheckboxStroke));
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            p.setPen(pen);
            const QRectF r = d.rect;
            auto at = [&r](const QPointF &fraction) {
                return QPointF(r.left() + r.width() * fraction.x(), r.top() + r.height() * fraction.y());
            };
            const QPointF pts[] = {at(TickStart), at(TickCorner), at(TickEnd)};
            p.drawPolyline(pts, 3);
        } else {
            p.setPen(QPen(m_theme.muted, 1));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(d.rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
        }
        break;
    }
    case Deco::Text:
        p.setFont(d.font);
        p.setPen(d.color);
        p.drawText(d.rect, Qt::AlignRight | Qt::AlignVCenter | Qt::TextDontClip, d.text);
        break;
    case Deco::Triangle: {
        const QRectF r = d.rect;
        QPolygonF tri;
        if (d.on)
            tri << r.topLeft() << r.topRight() << QPointF(r.center().x(), r.bottom());
        else
            tri << r.topLeft() << QPointF(r.right(), r.center().y()) << r.bottomLeft();
        p.setPen(Qt::NoPen);
        p.setBrush(d.color);
        p.drawPolygon(tri);
        break;
    }
    }
}

void Layout::paintText(QPainter &p, int index, const PaintState &state) const
{
    const TextBox &b = *texts[index];
    paintInlineBackgrounds(p, b, m_theme);
    paintFindMatches(p, b, index, state, m_theme);
    QList<QTextLayout::FormatRange> overlays;
    paintSelection(p, b, index, state, m_theme, overlays);
    underlineHoveredLink(b, index, state, overlays);
    b.layout.draw(&p, b.pos, overlays);
    paintImages(p, b, state, m_theme);
}

void Layout::paint(QPainter &painter, const QRectF &clip, const PaintState &state) const
{
    painter.setRenderHint(QPainter::Antialiasing, true);

    auto withFrame = [&](int frame, const QRectF &rect, auto &&draw) {
        if (frame < 0) {
            if (rect.bottom() >= clip.top() && rect.top() <= clip.bottom())
                draw();
            return;
        }
        const ScrollFrame &f = frames[frame];
        if (f.rect.bottom() < clip.top() || f.rect.top() > clip.bottom() || rect.bottom() < clip.top()
            || rect.top() > clip.bottom())
            return;
        painter.save();
        painter.setClipRect(f.rect, Qt::IntersectClip);
        painter.translate(-f.offset, 0);
        draw();
        painter.restore();
    };

    for (const Deco &d : decos)
        withFrame(d.frame, d.rect, [&] { paintDeco(painter, d); });
    for (size_t i = 0; i < texts.size(); ++i) {
        const TextBox &b = *texts[i];
        withFrame(b.frame, b.rect(), [&] { paintText(painter, int(i), state); });
    }
}

} // namespace md
