#include "view/documentview.h"

// DocumentView core: setup, layout, horizontal scroll frames, navigation,
// painting and resizing. Selection, find and copy buttons are in
// documentview_text.cpp; mouse, keyboard and menus in documentview_input.cpp.

#include "highlight/highlighter.h"

#include <QElapsedTimer>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>

#include <cmath>
#include <limits>

using namespace md;

namespace {
// A layout faster than this is redone on every resize step; slower ones wait
// until resizing pauses for ResizeDebounceMs.
constexpr qint64 CheapLayoutMs = 12;
constexpr int ResizeDebounceMs = 90;
// In em:
constexpr qreal FrameBarHeight = 0.55; // the scrollbar under a sideways-scrolling block
constexpr qreal FrameBarInset = 0.3;
constexpr qreal FrameBarStep = 2;       // scrolled per click on the bar's arrows
constexpr int MinFrameBarHeight = 8;    // pixels, so the bar can still be grabbed when zoomed out
constexpr int FrameBarBottomGap = 2;    // pixels between the bar and the block's bottom edge
constexpr qreal ScrollTargetGap = 1;    // above a block scrolled to, such as a heading
constexpr qreal CurrentHeadingLine = 2; // how far into the view a heading becomes current
constexpr qreal WheelLineStep = 3;      // em scrolled per wheel line or arrow step
constexpr int MaxAutoScrollStep = 80;   // pixels per tick while drag-selecting past an edge
// The tallest document the scrollbar is told about. A hostile document can
// lay out taller than an int holds, and converting such a height is undefined.
constexpr int MaxScrollExtent = std::numeric_limits<int>::max() / 2;
} // namespace

DocumentView::DocumentView(QWidget *parent)
    : QAbstractScrollArea(parent)
    , m_highlighter(std::make_unique<CodeHighlighter>())
{
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    viewport()->setMouseTracking(true);
    viewport()->setCursor(Qt::IBeamCursor);
    setFocusPolicy(Qt::StrongFocus);
    rebuildTheme();
}

DocumentView::~DocumentView() = default;

void DocumentView::rebuildTheme()
{
    m_theme = Theme::fromPalette(palette(), m_zoom);
    m_highlighter->setDark(m_theme.dark);
    m_boxesReusable = false;
    verticalScrollBar()->setSingleStep(qRound(m_theme.em(WheelLineStep)));
}

qreal DocumentView::verticalPadding() const
{
    return m_theme.contentMargin();
}

qreal DocumentView::documentHeight() const
{
    return (m_layout ? m_layout->height() : 0) + 2 * verticalPadding();
}

qreal DocumentView::scrollY() const
{
    return verticalScrollBar()->value();
}

QPointF DocumentView::toDocument(const QPoint &viewportPos) const
{
    return QPointF(viewportPos.x() - m_contentX, viewportPos.y() + scrollY() - verticalPadding());
}

QPoint DocumentView::toViewport(const QPointF &docPoint) const
{
    return QPointF(docPoint.x() + m_contentX, docPoint.y() - scrollY() + verticalPadding()).toPoint();
}

void DocumentView::setImageSource(ImageSource *images)
{
    m_images = images;
}

void DocumentView::setDocument(std::unique_ptr<Document> doc, bool keepPosition)
{
    int line = -1;
    qreal delta = 0;
    if (keepPosition && m_layout) {
        if (const Block *b = m_layout->blockAt(scrollY() - verticalPadding())) {
            line = b->sourceLine;
            delta = scrollY() - m_layout->blockTop(b);
        }
    } else if (keepPosition && m_layoutPending) {
        line = m_pendingLine; // reloaded again before it was ever shown
        delta = m_pendingDelta;
    }

    m_layout.reset(); // holds pointers into the old document
    m_pendingBlock = nullptr;
    m_highlighter->clear();
    m_toggled.clear();
    m_anchor = m_cursor = Cursor();
    m_copyTarget = m_copied = CopyTarget();
    m_hoverBox = m_hoverLink = -1;
    // A press on the old document must not complete on the new one.
    m_selecting = m_pressMoved = false;
    m_pressLink.clear();
    m_pressToggle = nullptr;
    m_autoScroll.stop();
    m_doc = std::move(doc);

    m_pendingLine = line;
    m_pendingDelta = delta;
    m_layoutPending = true;
    if (isVisible() || !m_doc) {
        layOutPending();
    } else {
        // Laid out in showEvent(); meanwhile there is nothing to show.
        runFind();
        updateScrollRange();
        updateFrameBars();
        viewport()->update();
        emit layoutChanged();
    }
}

void DocumentView::layOutPending()
{
    m_layoutPending = false;
    doLayout(nullptr, 0);
    verticalScrollBar()->setValue(0);
    if (m_layout && m_pendingBlock) {
        scrollToBlock(m_pendingBlock);
    } else if (m_layout && m_pendingLine >= 0) {
        if (const Block *b = m_layout->blockForLine(m_pendingLine))
            verticalScrollBar()->setValue(qRound(m_layout->blockTop(b) + m_pendingDelta));
    }
    m_pendingBlock = nullptr;
    m_pendingLine = -1;
    runFind();
}

void DocumentView::setWidthLimit(bool enabled, int pixels)
{
    if (m_limitWidth == enabled && m_limitPixels == pixels)
        return;
    m_limitWidth = enabled;
    m_limitPixels = qBound(ViewLimits::MinTextWidth, pixels, ViewLimits::MaxTextWidth);
    relayout();
}

void DocumentView::setWrapCode(bool wrap)
{
    if (m_wrapCode == wrap)
        return;
    m_wrapCode = wrap;
    m_boxesReusable = false;
    relayout();
}

void DocumentView::setZoom(qreal zoom)
{
    zoom = qBound(ViewLimits::MinZoom, zoom, ViewLimits::MaxZoom);
    if (qFuzzyCompare(zoom, m_zoom))
        return;
    m_zoom = zoom;
    rebuildTheme();
    relayout();
    emit zoomChanged(m_zoom);
}

void DocumentView::relayout()
{
    if (m_layoutPending)
        return; // showEvent() lays out, at the size the view is shown at
    const Block *anchor = nullptr;
    qreal delta = 0;
    if (m_layout) {
        anchor = m_layout->blockAt(scrollY() - verticalPadding());
        if (anchor)
            delta = scrollY() - m_layout->blockTop(anchor);
    }
    doLayout(anchor, delta);
}

void DocumentView::doLayout(const Block *anchorBlock, qreal anchorDelta)
{
    QHash<const Block *, qreal> offsets;
    if (m_layout) {
        for (const ScrollFrame &f : m_layout->frames) {
            if (f.offset > 0)
                offsets.insert(f.block, f.offset);
        }
    }

    if (!m_doc) {
        m_layout.reset();
        updateScrollRange();
        updateFrameBars();
        viewport()->update();
        emit layoutChanged();
        return;
    }

    QElapsedTimer timer;
    timer.start();

    LayoutOptions options;
    options.width = columnWidth();
    options.wrapCode = m_wrapCode;
    options.toggledDetails = &m_toggled;
    m_contentX = columnX(options.width);

    const size_t oldBoxes = m_layout ? m_layout->texts.size() : 0;
    std::unique_ptr<Layout> previous;
    if (m_boxesReusable)
        previous = std::move(m_layout);
    m_layout = std::make_unique<Layout>(*m_doc, m_theme, options, m_highlighter.get(), m_images, std::move(previous));
    m_boxesReusable = true;
    m_lastLayoutMs = timer.elapsed();

    for (ScrollFrame &f : m_layout->frames)
        f.offset = qMin(offsets.value(f.block, 0), f.maxOffset());
    if (m_layout->texts.size() != oldBoxes) {
        // Box indices no longer mean the same thing.
        m_anchor = m_cursor = Cursor();
    }
    m_copyTarget = CopyTarget();
    m_hoverBox = m_hoverLink = -1;

    updateScrollRange();
    if (anchorBlock) {
        const qreal top = m_layout->blockTop(anchorBlock);
        if (top >= 0)
            verticalScrollBar()->setValue(qRound(top + anchorDelta));
    }
    updateFrameBars();
    if (!m_findText.isEmpty()) {
        const int keep = m_currentMatch;
        runFind();
        // Fewer matches than before (a details block closed): stay on the last one.
        if (keep >= 0 && !m_matches.empty())
            m_currentMatch = qMin(keep, int(m_matches.size()) - 1);
    }
    viewport()->update();
    emit layoutChanged();
}

qreal DocumentView::columnWidth() const
{
    const qreal available = qMax(m_theme.minContentWidth(), viewport()->width() - 2 * m_theme.contentMargin());
    return m_limitWidth ? qMin<qreal>(available, m_limitPixels) : available;
}

qreal DocumentView::columnX(qreal width) const
{
    return std::floor(qMax(m_theme.contentMargin(), (viewport()->width() - width) / 2));
}

void DocumentView::updateScrollRange()
{
    const int page = viewport()->height();
    const int total = qCeil(qMin<qreal>(documentHeight(), MaxScrollExtent));
    verticalScrollBar()->setPageStep(page);
    verticalScrollBar()->setRange(0, qMax(0, total - page));
}

// Unwrapped code blocks and over-wide tables get their own small horizontal
// scrollbar. The bars are pooled child widgets placed over the visible frames.
void DocumentView::updateFrameBars()
{
    int used = 0;
    if (m_layout) {
        const qreal top = scrollY() - verticalPadding();
        const qreal bottom = top + viewport()->height();
        const int barHeight = qMax(MinFrameBarHeight, qRound(m_theme.em(FrameBarHeight)));
        for (size_t i = 0; i < m_layout->frames.size(); ++i) {
            ScrollFrame &f = m_layout->frames[i];
            if (f.maxOffset() <= 0 || f.rect.bottom() < top || f.rect.top() > bottom)
                continue;
            if (used == m_frameBars.size()) {
                auto *bar = new QScrollBar(Qt::Horizontal, viewport());
                bar->setCursor(Qt::ArrowCursor);
                bar->setFocusPolicy(Qt::NoFocus);
                connect(bar, &QScrollBar::valueChanged, this, [this, bar](int value) {
                    const int index = bar->property("frame").toInt();
                    if (m_layout && index >= 0 && index < int(m_layout->frames.size())) {
                        m_layout->frames[index].offset = value;
                        viewport()->update();
                    }
                });
                m_frameBars.append(bar);
            }
            QScrollBar *bar = m_frameBars[used++];
            const QSignalBlocker blocker(bar);
            bar->setProperty("frame", int(i));
            bar->setRange(0, qCeil(f.maxOffset()));
            bar->setPageStep(qRound(f.rect.width()));
            bar->setSingleStep(qRound(m_theme.em(FrameBarStep)));
            bar->setValue(qRound(f.offset));
            const int inset = qRound(m_theme.em(FrameBarInset));
            bar->setGeometry(qRound(m_contentX + f.rect.left()) + inset,
                             qRound(f.rect.bottom() - top) - barHeight - FrameBarBottomGap,
                             qRound(f.rect.width()) - 2 * inset, barHeight);
            bar->show();
        }
    }
    for (int i = used; i < m_frameBars.size(); ++i)
        m_frameBars[i]->hide();
}

int DocumentView::frameAt(const QPointF &docPoint) const
{
    if (!m_layout)
        return -1;
    for (size_t i = 0; i < m_layout->frames.size(); ++i) {
        if (m_layout->frames[i].rect.contains(docPoint))
            return int(i);
    }
    return -1;
}

// ------------------------------------------------------------ navigation

void DocumentView::scrollToBlock(const Block *block)
{
    if (m_layoutPending) {
        m_pendingBlock = block;
        return;
    }
    if (!m_layout || !block)
        return;
    const qreal top = m_layout->blockTop(block);
    if (top >= 0)
        verticalScrollBar()->setValue(qRound(top + verticalPadding() - m_theme.em(ScrollTargetGap)));
}

bool DocumentView::scrollToAnchor(const QString &anchor)
{
    if (!m_doc)
        return false;
    if (anchor.isEmpty()) {
        m_pendingBlock = nullptr;
        m_pendingLine = -1;
        verticalScrollBar()->setValue(0);
        return true;
    }
    const Block *block = m_doc->findAnchor(anchor);
    if (!block)
        block = m_doc->findAnchor(anchor.toLower());
    if (!block)
        return false;
    scrollToBlock(block);
    return true;
}

const Block *DocumentView::currentHeading() const
{
    if (!m_doc || !m_layout)
        return nullptr;
    // At the very end the last headings can never reach the top of the
    // viewport, so the bottom edge decides instead.
    const QScrollBar *bar = verticalScrollBar();
    const bool atEnd = bar->maximum() > 0 && bar->value() >= bar->maximum();
    // A heading counts as current once it is a little way into the viewport.
    const qreal y = scrollY() - verticalPadding() + (atEnd ? viewport()->height() : m_theme.em(CurrentHeadingLine));
    const Block *current = nullptr;
    for (const OutlineEntry &e : m_doc->outline) {
        const qreal top = m_layout->blockTop(e.block);
        if (top < 0)
            continue; // inside a collapsed details block
        if (top > y)
            break;
        current = e.block;
    }
    return current;
}

// ------------------------------------------------------------------ events

void DocumentView::paintEvent(QPaintEvent *event)
{
    QPainter p(viewport());
    p.fillRect(event->rect(), m_theme.background);
    if (!m_layout)
        return;

    const qreal dy = verticalPadding() - scrollY();
    p.translate(m_contentX, dy);
    const QRectF clip = QRectF(event->rect()).translated(-m_contentX, -dy);
    m_layout->paint(p, clip, paintState());

    if (m_copyTarget.kind != CopyTarget::None)
        paintCopyButton(p, m_copyTarget);
}

void DocumentView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    if (!m_layout || event->size().width() == event->oldSize().width()) {
        if (!m_layout && m_doc)
            relayout();
        updateScrollRange();
        updateFrameBars();
        return;
    }
    if (columnWidth() == m_layout->width()) {
        // Wider than the width limit either way (or the preview strip was
        // switched for a scrollbar): the text keeps its lines, and only the
        // column moves to stay centred. No layout, so it moves at once.
        m_resizeTimer.stop();
        m_contentX = columnX(m_layout->width());
        updateScrollRange();
        updateFrameBars();
        viewport()->update();
        return;
    }
    // Re-layout immediately while it is cheap; debounce for heavy documents.
    if (m_lastLayoutMs < CheapLayoutMs)
        relayout();
    else
        m_resizeTimer.start(ResizeDebounceMs, this);
}

void DocumentView::scrollContentsBy(int, int)
{
    updateFrameBars();
    viewport()->update();
}

void DocumentView::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_resizeTimer.timerId()) {
        m_resizeTimer.stop();
        relayout();
    } else if (event->timerId() == m_copiedTimer.timerId()) {
        m_copiedTimer.stop();
        m_copied = CopyTarget();
        viewport()->update();
    } else if (event->timerId() == m_autoScroll.timerId()) {
        const int y = m_lastMousePos.y();
        int delta = 0;
        if (y < 0)
            delta = y;
        else if (y > viewport()->height())
            delta = y - viewport()->height();
        if (delta == 0 || !m_selecting) {
            m_autoScroll.stop();
            return;
        }
        verticalScrollBar()->setValue(verticalScrollBar()->value()
                                      + qBound(-MaxAutoScrollStep, delta, MaxAutoScrollStep));
        setSelectionEnd(m_lastMousePos);
    } else {
        QAbstractScrollArea::timerEvent(event);
    }
}

void DocumentView::leaveEvent(QEvent *event)
{
    QAbstractScrollArea::leaveEvent(event);
    if (m_hoverLink >= 0 || m_copyTarget.kind != CopyTarget::None) {
        m_hoverBox = m_hoverLink = -1;
        m_copyTarget = CopyTarget();
        viewport()->update();
    }
    if (!m_hoverHref.isEmpty()) {
        m_hoverHref.clear();
        emit linkHovered(QString());
    }
}

void DocumentView::showEvent(QShowEvent *event)
{
    QAbstractScrollArea::showEvent(event);
    if (!m_layoutPending)
        return;
    // Not yet: a window shows its children before itself, and the tab widget
    // only sizes its pages in its own showEvent(), after this one. Laying out
    // now would be at the old size and then again at the real one. Once the
    // window has finished showing, the size is final.
    QMetaObject::invokeMethod(
        this,
        [this] {
            if (m_layoutPending && isVisible())
                layOutPending();
        },
        Qt::QueuedConnection);
}

void DocumentView::changeEvent(QEvent *event)
{
    QAbstractScrollArea::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::FontChange
        || event->type() == QEvent::ApplicationFontChange) {
        rebuildTheme();
        if (m_doc)
            relayout();
    }
}
