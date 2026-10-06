#include "layout/layoutinternal.h"

// Layout: the result of laying out a document, and the queries made on it
// (hit-testing, block positions). Building is in layoutbuilder.cpp, painting
// in layoutpaint.cpp, text boxes in textbox.cpp.

#include <algorithm>

namespace md {

namespace {
// Hit-testing picks the nearest box. A box on the pointer's own line must
// always beat one above or below it, so a pixel of vertical distance counts
// for more than any horizontal distance a document can have.
constexpr qreal VerticalDominance = 4096;
} // namespace

Layout::Layout(const Document &doc, const Theme &theme, const LayoutOptions &options, CodeHighlighter *highlighter,
               ImageSource *images)
    : m_theme(theme)
{
    buildLayout(*this, doc, options, highlighter, images);
}

const Block *Layout::blockAt(qreal y) const
{
    auto it = std::upper_bound(m_blockOrder.begin(), m_blockOrder.end(), y,
                               [](qreal value, const QPair<qreal, const Block *> &e) { return value < e.first; });
    if (it == m_blockOrder.begin())
        return m_blockOrder.isEmpty() ? nullptr : m_blockOrder.first().second;
    return (it - 1)->second;
}

const Block *Layout::blockForLine(int line) const
{
    const Block *last = nullptr;
    for (const auto &entry : m_blockOrder) {
        last = entry.second;
        if (entry.second->sourceLine >= line)
            break;
    }
    return last;
}

QRectF Layout::toDocument(const TextBox &box, const QRectF &local) const
{
    QRectF r = local.translated(box.pos);
    if (box.frame >= 0)
        r.translate(-frames[box.frame].offset, 0);
    return r;
}

Hit Layout::hitTest(const QPointF &point) const
{
    Hit hit;
    qreal best = std::numeric_limits<qreal>::max();
    QPointF bestPoint;
    for (size_t i = 0; i < texts.size(); ++i) {
        const TextBox &b = *texts[i];
        QPointF pt = point;
        if (b.frame >= 0)
            pt.rx() += frames[b.frame].offset;
        const QRectF r(b.pos, QSizeF(qMax(b.width, b.naturalWidth), b.height));
        const qreal dy = pt.y() < r.top() ? r.top() - pt.y() : pt.y() >= r.bottom() ? pt.y() - r.bottom() + 1 : 0;
        const qreal dx = pt.x() < r.left() ? r.left() - pt.x() : pt.x() >= r.right() ? pt.x() - r.right() + 1 : 0;
        const qreal distance = dy * VerticalDominance + dx;
        if (distance < best) {
            best = distance;
            hit.box = int(i);
            bestPoint = pt;
            if (distance == 0)
                break;
        }
    }
    if (hit.box < 0)
        return hit;
    const TextBox &b = *texts[hit.box];
    const QPointF local = bestPoint - b.pos;
    hit.pos = b.cursorAt(local);
    hit.inside = best == 0;
    if (hit.inside) {
        if (b.frame >= 0 && !frames[b.frame].rect.contains(point)) {
            hit.inside = false;
        } else {
            hit.image = b.imageAt(local);
            hit.span = b.spanAt(local);
        }
    }
    return hit;
}

const Span *Hit::inlineSpan(const TextBox &b) const
{
    // spanAt() only reports a span for boxes with inline text, so b.inl is set.
    if (span < 0)
        return nullptr;
    Q_ASSERT(b.inl);
    return &b.inl->spans[span];
}

QString Hit::href(const TextBox &b) const
{
    const int index = link(b);
    return index >= 0 && b.inl ? b.inl->links.value(index) : QString();
}

int Hit::link(const TextBox &b) const
{
    if (image && image->image->link >= 0)
        return image->image->link;
    if (span >= 0 && b.inl)
        return b.inl->spans[span].link;
    return -1;
}

} // namespace md
