#include "view/documentview.h"

// DocumentView: mouse, wheel, keyboard and context menu handling.

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QScrollBar>
#include <QToolTip>

#include <cmath>

using namespace md;

namespace {
constexpr int AutoScrollIntervalMs = 30; // while drag-selecting past an edge
constexpr double WheelNotch = 120;       // angle delta of one wheel step
constexpr int AngleDeltaPerPixel = 2;    // a wheel without pixel deltas scrolls a frame half a notch's units
constexpr qreal PageOverlap = 3;         // em of the old page still visible after Space
} // namespace

void DocumentView::updateHover(const QPoint &viewportPos)
{
    if (!m_layout)
        return;
    const QPointF docPoint = toDocument(viewportPos);
    const Hit hit = m_layout->hitTest(docPoint);

    int hoverBox = -1;
    int hoverLink = -1;
    QString href;
    if (hit.inside) {
        const TextBox &b = *m_layout->texts[hit.box];
        href = hit.href(b);
        if (!href.isEmpty()) {
            hoverBox = hit.box;
            hoverLink = hit.link(b);
        }
    }

    const CopyTarget target = copyTargetAt(docPoint, hit);
    const bool overButton = target.kind != CopyTarget::None && target.button.contains(docPoint);

    bool overToggle = false;
    for (const ToggleArea &t : m_layout->toggles) {
        if (t.rect.contains(docPoint)) {
            overToggle = true;
            break;
        }
    }

    Qt::CursorShape shape = Qt::IBeamCursor;
    if (overButton || hoverLink >= 0 || overToggle)
        shape = Qt::PointingHandCursor;
    else if (!hit.inside)
        shape = Qt::ArrowCursor;
    if (viewport()->cursor().shape() != shape)
        viewport()->setCursor(shape);

    if (overButton)
        QToolTip::showText(viewport()->mapToGlobal(viewportPos),
                           m_copied == target ? tr("Copied") : tr("Copy to clipboard"), viewport());

    if (href != m_hoverHref) {
        m_hoverHref = href;
        emit linkHovered(href);
    }
    // The button's hot state follows the pointer, so repaint while one is shown.
    if (hoverBox != m_hoverBox || hoverLink != m_hoverLink || !(target == m_copyTarget)
        || target.kind != CopyTarget::None) {
        m_hoverBox = hoverBox;
        m_hoverLink = hoverLink;
        m_copyTarget = target;
        viewport()->update();
    }
}

void DocumentView::mousePressEvent(QMouseEvent *event)
{
    if (!m_layout) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }
    const QPointF docPoint = toDocument(event->pos());
    m_pressPos = event->pos();
    m_pressMoved = false;
    m_pressLink.clear();
    m_pressToggle = nullptr;

    const Hit hit = m_layout->hitTest(docPoint);
    if (hit.inside)
        m_pressLink = hit.href(*m_layout->texts[hit.box]);

    if (event->button() == Qt::MiddleButton) {
        if (!m_pressLink.isEmpty())
            emit linkActivated(m_pressLink, OpenIn::NewTab);
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;

    if (m_copyTarget.kind != CopyTarget::None && m_copyTarget.button.contains(docPoint)) {
        performCopy(m_copyTarget);
        return;
    }
    for (const ToggleArea &t : m_layout->toggles) {
        if (t.rect.contains(docPoint)) {
            m_pressToggle = t.block;
            break;
        }
    }
    if (hit.box < 0)
        return;

    m_selecting = true;
    if (event->modifiers() & Qt::ShiftModifier && m_anchor.box >= 0) {
        m_cursor = Cursor {hit.box, hit.pos};
    } else {
        m_anchor = m_cursor = Cursor {hit.box, hit.pos};
    }
    viewport()->update();
}

void DocumentView::mouseMoveEvent(QMouseEvent *event)
{
    m_lastMousePos = event->pos();
    if (m_selecting && !(event->buttons() & Qt::LeftButton)) {
        // The release went elsewhere (a popup took the pointer), so the drag
        // is over and the next press must not continue it.
        m_selecting = false;
        m_autoScroll.stop();
    }
    if (m_selecting) {
        if ((event->pos() - m_pressPos).manhattanLength() >= QApplication::startDragDistance())
            m_pressMoved = true;
        if (m_pressMoved) {
            setSelectionEnd(event->pos());
            const bool outside = event->pos().y() < 0 || event->pos().y() > viewport()->height();
            if (outside && !m_autoScroll.isActive())
                m_autoScroll.start(AutoScrollIntervalMs, this);
        }
        return;
    }
    updateHover(event->pos());
}

void DocumentView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const bool wasSelecting = m_selecting;
    m_selecting = false;
    m_autoScroll.stop();
    if (!wasSelecting)
        return;

    if (!m_pressMoved) {
        if (m_pressToggle) {
            // Clicking a summary opens or closes its details block.
            if (!m_toggled.remove(m_pressToggle))
                m_toggled.insert(m_pressToggle);
            m_pressToggle = nullptr;
            relayout();
            return;
        }
        if (!m_pressLink.isEmpty()) {
            emit linkActivated(m_pressLink,
                               event->modifiers() & Qt::ControlModifier ? OpenIn::NewTab : OpenIn::CurrentTab);
            return;
        }
    }
    if (hasSelection() && QApplication::clipboard()->supportsSelection())
        QApplication::clipboard()->setText(selectedText(), QClipboard::Selection);
}

void DocumentView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!m_layout || event->button() != Qt::LeftButton)
        return;
    const Hit hit = m_layout->hitTest(toDocument(event->pos()));
    if (hit.box < 0 || !hit.inside)
        return;
    const QString text = m_layout->texts[hit.box]->layout.text();
    auto isWord = [&](int i) { return i >= 0 && i < text.size() && (text[i].isLetterOrNumber() || text[i] == u'_'); };
    int start = qMin(hit.pos, int(text.size()));
    int end = start;
    if (!isWord(start) && isWord(start - 1)) {
        --start;
        --end;
    }
    while (isWord(start - 1))
        --start;
    while (isWord(end))
        ++end;
    if (end > start) {
        m_anchor = Cursor {hit.box, start};
        m_cursor = Cursor {hit.box, end};
        m_pressToggle = nullptr;
        m_pressLink.clear();
        if (QApplication::clipboard()->supportsSelection())
            QApplication::clipboard()->setText(selectedText(), QClipboard::Selection);
        viewport()->update();
    }
}

void DocumentView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const int steps = event->angleDelta().y();
        if (steps != 0)
            setZoom(m_zoom * std::pow(ViewLimits::ZoomStep, steps / WheelNotch));
        event->accept();
        return;
    }

    // Horizontal wheel motion (or Shift+wheel) scrolls the code block or table under the pointer.
    QPoint delta = event->pixelDelta().isNull() ? event->angleDelta() / AngleDeltaPerPixel : event->pixelDelta();
    if (event->modifiers() & Qt::ShiftModifier && delta.x() == 0)
        delta = QPoint(delta.y(), 0);
    if (m_layout && delta.x() != 0 && qAbs(delta.x()) > qAbs(delta.y())) {
        const int index = frameAt(toDocument(event->position().toPoint()));
        if (index >= 0) {
            ScrollFrame &f = m_layout->frames[index];
            f.offset = qBound(0.0, f.offset - delta.x(), f.maxOffset());
            updateFrameBars();
            viewport()->update();
            event->accept();
            return;
        }
    }
    QAbstractScrollArea::wheelEvent(event);
}

void DocumentView::keyPressEvent(QKeyEvent *event)
{
    QScrollBar *bar = verticalScrollBar();
    if (event->matches(QKeySequence::Copy)) {
        copy();
    } else if (event->matches(QKeySequence::SelectAll)) {
        selectAll();
    } else if (event->key() == Qt::Key_Home) {
        bar->setValue(0);
    } else if (event->key() == Qt::Key_End) {
        bar->setValue(bar->maximum());
    } else if (event->key() == Qt::Key_Space) {
        const int page = viewport()->height() - qRound(m_theme.em(PageOverlap));
        bar->setValue(bar->value() + (event->modifiers() & Qt::ShiftModifier ? -page : page));
    } else {
        QAbstractScrollArea::keyPressEvent(event);
    }
}

void DocumentView::contextMenuEvent(QContextMenuEvent *event)
{
    if (!m_layout)
        return;
    const QPointF docPoint = toDocument(viewport()->mapFromGlobal(event->globalPos()));
    const Hit hit = m_layout->hitTest(docPoint);

    QString href;
    QString inlineCode;
    const Block *codeBlock = nullptr;
    if (hit.inside) {
        const TextBox &b = *m_layout->texts[hit.box];
        href = hit.href(b);
        if (const Span *span = hit.inlineSpan(b); span && (span->flags & Span::Code))
            inlineCode = b.copyText(span->start, span->length);
    }
    for (const CodeArea &area : m_layout->codeAreas) {
        if (area.rect.contains(docPoint))
            codeBlock = area.block;
    }

    QMenu menu(this);
    QAction *copyAction =
        menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), tr("&Copy"), this, &DocumentView::copy);
    copyAction->setEnabled(hasSelection());
    copyAction->setShortcut(QKeySequence::Copy);
    if (codeBlock) {
        menu.addAction(tr("Copy Code &Block"), this,
                       [code = codeBlock->code] { QApplication::clipboard()->setText(code); });
    }
    if (!inlineCode.isEmpty())
        menu.addAction(tr("Copy C&ode"), this, [inlineCode] { QApplication::clipboard()->setText(inlineCode); });
    if (!href.isEmpty()) {
        menu.addSeparator();
        menu.addAction(tr("Copy &Link Address"), this, [href] { QApplication::clipboard()->setText(href); });
        menu.addAction(tr("Open Link in New &Tab"), this, [this, href] { emit linkActivated(href, OpenIn::NewTab); });
    }
    menu.addSeparator();
    QAction *all = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-select-all")), tr("Select &All"), this,
                                  &DocumentView::selectAll);
    all->setShortcut(QKeySequence::SelectAll);
    menu.exec(event->globalPos());
}
