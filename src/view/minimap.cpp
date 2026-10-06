#include "view/minimap.h"

#include "view/documentview.h"

#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>

namespace {
constexpr int TileHeight = 256;
constexpr qreal SidePadding = 6;
constexpr int CachedTiles = 24; // rendered tiles kept, several screens of preview
constexpr int StripWidth = 112;
constexpr int MinStripHeight = 200;
constexpr qreal MinBoxHeight = 8; // the viewport box stays grabbable on very long documents
// How strongly the viewport box is tinted with the text colour.
constexpr qreal BoxTintDragging = 0.22;
constexpr qreal BoxTintHovered = 0.17;
constexpr qreal BoxTint = 0.11;
constexpr qreal EdgeTint = 0.12;  // the line separating the strip from the document
constexpr int WheelPixelGain = 2; // a touchpad scroll over the strip moves the document this much faster
} // namespace

Minimap::Minimap(DocumentView *view, QWidget *parent)
    : QWidget(parent)
    , m_view(view)
    , m_tiles(CachedTiles)
{
    setFixedWidth(sizeHint().width());
    setCursor(Qt::ArrowCursor);
    connect(view, &DocumentView::layoutChanged, this, &Minimap::invalidate);
    connect(view->verticalScrollBar(), &QScrollBar::valueChanged, this, qOverload<>(&QWidget::update));
    connect(view->verticalScrollBar(), &QScrollBar::rangeChanged, this, qOverload<>(&QWidget::update));
}

QSize Minimap::sizeHint() const
{
    return QSize(StripWidth, MinStripHeight);
}

void Minimap::invalidate()
{
    m_tiles.clear();
    update();
}

Minimap::Geometry Minimap::geometryNow() const
{
    Geometry g;
    const md::Layout *layout = m_view->documentLayout();
    if (!layout || layout->width() <= 0)
        return g;
    g.scale = (width() - 2 * SidePadding) / layout->width();

    const QScrollBar *bar = m_view->verticalScrollBar();
    const qreal total = m_view->documentHeight() * g.scale;
    const qreal overflow = qMax(0.0, total - height());
    const qreal maxScroll = qMax(1, bar->maximum());
    // When the preview is taller than the strip it scrolls proportionally,
    // so the box always stays reachable.
    g.boxSlope = g.scale - overflow / maxScroll;
    g.offset = overflow * bar->value() / maxScroll;
    g.boxTop = bar->value() * g.boxSlope;
    g.boxHeight = qMax(MinBoxHeight, m_view->viewport()->height() * g.scale);
    return g;
}

QImage Minimap::tile(int index, const Geometry &g)
{
    if (const QImage *cached = m_tiles.object(index))
        return *cached;

    const md::Layout *layout = m_view->documentLayout();
    const qreal dpr = devicePixelRatioF();
    auto *image = new QImage(QSize(width(), TileHeight) * dpr, QImage::Format_RGB32);
    image->setDevicePixelRatio(dpr);
    image->fill(layout->theme().background);

    QPainter p(image);
    p.translate(SidePadding, -index * TileHeight);
    p.scale(g.scale, g.scale);
    p.translate(0, m_view->verticalPadding());
    const qreal top = index * TileHeight / g.scale - m_view->verticalPadding();
    md::PaintState state;
    state.images = m_view->imageSource();
    state.preview = true;
    layout->paint(p, QRectF(-SidePadding / g.scale, top, width() / g.scale, TileHeight / g.scale), state);
    p.end();

    // QCache owns the image after insert() and may delete it at once if it
    // does not fit, so take the (implicitly shared) copy first.
    QImage result = *image;
    m_tiles.insert(index, image);
    return result;
}

void Minimap::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const md::Layout *layout = m_view->documentLayout();
    const QColor background = layout ? layout->theme().background : palette().color(QPalette::Base);
    const QColor text = layout ? layout->theme().text : palette().color(QPalette::Text);
    p.fillRect(rect(), background);
    if (layout) {
        const Geometry g = geometryNow();
        const qreal total = m_view->documentHeight() * g.scale;
        const int first = int(g.offset) / TileHeight;
        const int last = int(qMin(total, g.offset + height())) / TileHeight;
        for (int i = first; i <= last; ++i)
            p.drawImage(QPointF(0, i * TileHeight - g.offset), tile(i, g));

        QColor box = text;
        box.setAlphaF(m_dragging ? BoxTintDragging : m_hovered ? BoxTintHovered : BoxTint);
        p.fillRect(QRectF(0, g.boxTop, width(), g.boxHeight), box);
    }
    QColor edge = text;
    edge.setAlphaF(EdgeTint);
    p.fillRect(QRect(0, 0, 1, height()), edge);
}

void Minimap::scrollTo(qreal boxTop)
{
    const Geometry g = geometryNow();
    if (g.boxSlope <= 0)
        return;
    m_view->verticalScrollBar()->setValue(qRound(boxTop / g.boxSlope));
}

void Minimap::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_view->documentLayout())
        return;
    const Geometry g = geometryNow();
    const qreal y = event->position().y();
    if (y < g.boxTop || y > g.boxTop + g.boxHeight) {
        // Jump so the clicked spot ends up in the middle of the viewport.
        scrollTo(y - g.boxHeight / 2);
        m_grab = g.boxHeight / 2;
    } else {
        m_grab = y - g.boxTop;
    }
    m_dragging = true;
    update();
}

void Minimap::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging)
        scrollTo(event->position().y() - m_grab);
}

void Minimap::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        update();
    }
}

void Minimap::wheelEvent(QWheelEvent *event)
{
    QScrollBar *bar = m_view->verticalScrollBar();
    const int delta = event->pixelDelta().isNull() ? event->angleDelta().y() : event->pixelDelta().y() * WheelPixelGain;
    bar->setValue(bar->value() - delta);
    event->accept();
}

void Minimap::enterEvent(QEnterEvent *)
{
    m_hovered = true;
    update();
}

void Minimap::leaveEvent(QEvent *)
{
    m_hovered = false;
    update();
}
