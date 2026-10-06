#include "view/documentview.h"

// DocumentView: text selection, find, and the copy buttons on code.

#include <QApplication>
#include <QClipboard>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>

#include <algorithm>

using namespace md;

namespace {
constexpr int CopiedFeedbackMs = 1500; // how long a copy button shows its tick
// In em:
constexpr qreal FindMarginTop = 1; // a match closer to an edge than this is scrolled to
constexpr qreal FindMarginBottom = 3;
constexpr qreal FindRevealDivisor = 3; // a match scrolled to lands this fraction of the way in: a third
// In pixels:
constexpr qreal CopyButtonSlack = 3;      // the pointer may overshoot a button by this much and keep it
constexpr qreal BlockCopyMinTop = 2;      // a block's button never rises closer than this to its top edge
constexpr qreal CopyButtonApproach = 0.7; // room to move from inline code to its button
constexpr qreal InlineCopyButton = 1.15;  // smallest size of an inline code's button
constexpr qreal InlineCopyGap = 0.25;     // between inline code and its button
constexpr qreal BlockCopyButton = 1.9;
constexpr qreal BlockCopyInset = 0.45; // from the code block's corner
constexpr qreal InlineCopyRadius = 0.2;
constexpr qreal BlockCopyRadius = 0.3;
constexpr qreal HotButtonTint = 0.12; // how far a hovered button moves towards the text colour
// The copy icon, as fractions of the button: its inset, stroke, and the size
// and corner of each of its two sheets. The tick's points are its shape.
constexpr qreal IconInsetX = 0.26;
constexpr qreal IconInsetY = 0.24;
constexpr qreal IconStroke = 0.07;
constexpr qreal MinIconStroke = 1.2; // pixels
constexpr qreal SheetWidth = 0.72;
constexpr qreal SheetHeight = 0.78;
constexpr qreal SheetCorner = 0.18;
} // namespace

// ------------------------------------------------------------- selection

bool DocumentView::hasSelection() const
{
    return m_anchor.box >= 0 && m_cursor.box >= 0 && !(m_anchor == m_cursor);
}

QString DocumentView::selectedText() const
{
    if (!hasSelection() || !m_layout)
        return {};
    const Cursor from = std::min(m_anchor, m_cursor);
    const Cursor to = std::max(m_anchor, m_cursor);
    QString out;
    for (int i = from.box; i <= to.box && i < int(m_layout->texts.size()); ++i) {
        const TextBox &b = *m_layout->texts[i];
        const int start = i == from.box ? from.pos : 0;
        const int end = i == to.box ? to.pos : b.layout.text().size();
        out += b.copyText(start, end - start);
        if (i != to.box)
            out += b.separator;
    }
    return out;
}

void DocumentView::copy()
{
    const QString text = selectedText();
    if (!text.isEmpty())
        QApplication::clipboard()->setText(text);
}

void DocumentView::selectAll()
{
    if (!m_layout || m_layout->texts.empty())
        return;
    m_anchor = Cursor {0, 0};
    const int last = int(m_layout->texts.size()) - 1;
    m_cursor = Cursor {last, int(m_layout->texts[last]->layout.text().size())};
    viewport()->update();
}

void DocumentView::setSelectionEnd(const QPoint &viewportPos)
{
    if (!m_layout)
        return;
    const Hit hit = m_layout->hitTest(toDocument(viewportPos));
    if (hit.box < 0)
        return;
    const Cursor c {hit.box, hit.pos};
    if (!(c == m_cursor)) {
        m_cursor = c;
        viewport()->update();
    }
}

PaintState DocumentView::paintState() const
{
    PaintState s;
    if (hasSelection()) {
        const Cursor from = std::min(m_anchor, m_cursor);
        const Cursor to = std::max(m_anchor, m_cursor);
        s.selStartBox = from.box;
        s.selStartPos = from.pos;
        s.selEndBox = to.box;
        s.selEndPos = to.pos;
    }
    s.hoverBox = m_hoverBox;
    s.hoverLink = m_hoverLink;
    s.matches = &m_matches;
    s.currentMatch = m_currentMatch;
    s.images = m_images;
    return s;
}

// ------------------------------------------------------------------ find

int DocumentView::find(const QString &text, bool caseSensitive)
{
    m_findText = text;
    m_findCaseSensitive = caseSensitive;
    runFind();
    if (!m_matches.empty() && m_layout) {
        // Start from the first match at or below the top of the viewport.
        const qreal top = scrollY() - verticalPadding();
        int index = 0;
        for (size_t i = 0; i < m_matches.size(); ++i) {
            if (m_layout->texts[m_matches[i].box]->rect().bottom() >= top) {
                index = int(i);
                break;
            }
        }
        showMatch(index);
    }
    viewport()->update();
    return int(m_matches.size());
}

void DocumentView::runFind()
{
    m_matches.clear();
    m_currentMatch = -1;
    if (m_findText.isEmpty() || !m_layout)
        return;
    const auto cs = m_findCaseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    for (size_t i = 0; i < m_layout->texts.size(); ++i) {
        const QString text = m_layout->texts[i]->layout.text();
        qsizetype pos = 0;
        while ((pos = text.indexOf(m_findText, pos, cs)) >= 0) {
            m_matches.push_back(FindMatch {int(i), int(pos), int(m_findText.size())});
            pos += m_findText.size();
        }
    }
}

void DocumentView::findNext(FindDirection direction)
{
    if (m_matches.empty())
        return;
    const bool forward = direction == FindDirection::Forward;
    const int n = int(m_matches.size());
    // With no current match (a reload left the matches but not the position),
    // forwards starts at the first match and backwards at the last.
    const int from = m_currentMatch >= 0 ? m_currentMatch : (forward ? -1 : 0);
    showMatch(((from + (forward ? 1 : -1)) % n + n) % n);
}

void DocumentView::showMatch(int index)
{
    if (!m_layout || index < 0 || index >= int(m_matches.size()))
        return;
    m_currentMatch = index;
    const FindMatch &m = m_matches[index];
    const TextBox &b = *m_layout->texts[m.box];
    const auto rects = b.rangeRects(m.start, m.length, LineBox::FullLine);
    if (!rects.isEmpty()) {
        if (b.frame >= 0) {
            // Bring the match into view horizontally inside its scroll frame.
            ScrollFrame &f = m_layout->frames[b.frame];
            const qreal x = b.pos.x() + rects.first().left() - f.rect.left();
            if (x < f.offset || x + rects.first().width() > f.offset + f.rect.width())
                f.offset = qBound(0.0, x - f.rect.width() / FindRevealDivisor, f.maxOffset());
        }
        const qreal y = b.pos.y() + rects.first().top() + verticalPadding();
        const qreal viewTop = scrollY();
        if (y < viewTop + m_theme.em(FindMarginTop)
            || y > viewTop + viewport()->height() - m_theme.em(FindMarginBottom))
            verticalScrollBar()->setValue(qRound(y - viewport()->height() / FindRevealDivisor));
        updateFrameBars();
    }
    viewport()->update();
}

void DocumentView::clearFind()
{
    m_findText.clear();
    m_matches.clear();
    m_currentMatch = -1;
    viewport()->update();
}

// ------------------------------------------------------- copy affordances

DocumentView::CopyTarget DocumentView::copyTargetAt(const QPointF &docPoint, const Hit &hit) const
{
    CopyTarget target;
    if (!m_layout)
        return target;

    // The pointer may be travelling from the code to its button: keep the current target.
    if (m_copyTarget.kind != CopyTarget::None
        && m_copyTarget.button
               .adjusted(-m_theme.em(CopyButtonApproach), -CopyButtonSlack, CopyButtonSlack, CopyButtonSlack)
               .contains(docPoint))
        return m_copyTarget;

    if (const Span *found = hit.inside ? hit.inlineSpan(*m_layout->texts[hit.box]) : nullptr) {
        const TextBox &b = *m_layout->texts[hit.box];
        const Span &span = *found;
        if (span.flags & Span::Code) {
            const auto rects = b.rangeRects(span.start, span.length, LineBox::Glyphs);
            if (!rects.isEmpty()) {
                const QRectF last = m_layout->toDocument(b, rects.last());
                const qreal size = qMax(m_theme.em(InlineCopyButton), last.height());
                target.kind = CopyTarget::InlineCode;
                target.box = hit.box;
                target.span = hit.span;
                target.button =
                    QRectF(last.right() + m_theme.em(InlineCopyGap), last.center().y() - size / 2, size, size);
                return target;
            }
        }
    }

    for (const CodeArea &area : m_layout->codeAreas) {
        if (!area.rect.contains(docPoint))
            continue;
        const qreal size = m_theme.em(BlockCopyButton);
        const qreal inset = m_theme.em(BlockCopyInset);
        // Keep the button reachable while the top of a long block is scrolled away.
        const qreal viewTop = scrollY() - verticalPadding();
        const qreal top = qMin(qMax(area.rect.top(), viewTop) + inset, area.rect.bottom() - size - inset);
        target.kind = CopyTarget::CodeBlock;
        target.block = area.block;
        target.button =
            QRectF(area.rect.right() - size - inset, qMax(top, area.rect.top() + BlockCopyMinTop), size, size);
        return target;
    }
    return target;
}

void DocumentView::performCopy(const CopyTarget &target)
{
    QString text;
    if (target.kind == CopyTarget::CodeBlock && target.block) {
        text = target.block->code;
    } else if (target.kind == CopyTarget::InlineCode && m_layout && target.box < int(m_layout->texts.size())) {
        const TextBox &b = *m_layout->texts[target.box];
        const Span &span = b.inl->spans[target.span];
        text = b.copyText(span.start, span.length);
    }
    if (text.isEmpty())
        return;
    QApplication::clipboard()->setText(text);
    m_copied = target;
    m_copiedTimer.start(CopiedFeedbackMs, this);
    viewport()->update();
}

void DocumentView::paintCopyButton(QPainter &p, const CopyTarget &target) const
{
    const QRectF r = target.button;
    const bool done = m_copied == target;
    const bool small = target.kind == CopyTarget::InlineCode;
    const QPointF mouse = toDocument(viewport()->mapFromGlobal(QCursor::pos()));
    const bool hot = r.contains(mouse);

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(hot ? m_theme.muted : m_theme.border, 1));
    p.setBrush(hot ? mixColors(m_theme.background, m_theme.text, HotButtonTint) : m_theme.background);
    const qreal radius = m_theme.em(small ? InlineCopyRadius : BlockCopyRadius);
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);

    const qreal insetX = r.width() * IconInsetX;
    const qreal insetY = r.height() * IconInsetY;
    const QRectF icon = r.adjusted(insetX, insetY, -insetX, -insetY);
    QPen pen(done ? m_theme.success : m_theme.muted, qMax(MinIconStroke, r.width() * IconStroke));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    if (done) {
        const QPointF pts[] = {
            {icon.left(), icon.center().y()},
            {icon.left() + icon.width() * 0.36, icon.bottom() - icon.height() * 0.12},
            {icon.right(), icon.top() + icon.height() * 0.12},
        };
        p.drawPolyline(pts, 3);
    } else {
        // Two overlapping sheets
        const qreal w = icon.width() * SheetWidth;
        const qreal h = icon.height() * SheetHeight;
        const qreal corner = w * SheetCorner;
        p.drawRoundedRect(QRectF(icon.left(), icon.top(), w, h), corner, corner);
        QPainterPath front;
        front.addRoundedRect(QRectF(icon.right() - w, icon.bottom() - h, w, h), corner, corner);
        p.fillPath(front, hot ? mixColors(m_theme.background, m_theme.text, HotButtonTint) : m_theme.background);
        p.drawPath(front);
    }
}
