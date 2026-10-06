#include "print/printing.h"

#include "highlight/highlighter.h"
#include "layout/layout.h"

#include <QPainter>
#include <QPrinter>

#include <algorithm>

namespace {
// Printing lays out in CSS-like pixels: 96 per inch.
constexpr qreal CssPixelsPerInch = 96.0;
constexpr qreal PrintBodyPx = 14;  // body text; about 10.5 pt
constexpr qreal FooterHeight = 28; // reserved at the bottom of each page for "n / total"
constexpr qreal FooterGap = 8;
constexpr int FooterFontPx = 10;
// A break may move up to keep a line or row whole, but never by more than
// this fraction of a page; anything taller is cut instead.
constexpr qreal MaxBreakShift = 0.8;
} // namespace

namespace md {

QList<qreal> paginate(const Layout &layout, qreal pageHeight)
{
    // Everything a break must not cut through, sorted by top edge.
    QList<QPair<qreal, qreal>> solid = layout.unbreakable;
    for (const auto &box : layout.texts) {
        for (const auto &extent : box->lineExtents())
            solid.append({box->pos.y() + extent.first, box->pos.y() + extent.second});
    }
    std::sort(solid.begin(), solid.end());

    QList<qreal> breaks;
    const qreal total = layout.height();
    // A page with no room would never advance `top`: treat it as one page.
    if (pageHeight <= 0) {
        breaks.append(total);
        return breaks;
    }
    qreal top = 0;
    while (total - top > pageHeight) {
        qreal y = top + pageHeight;
        // Move the break up until it no longer lands inside anything solid.
        bool moved = true;
        while (moved) {
            moved = false;
            for (const auto &extent : std::as_const(solid)) {
                if (extent.first >= y)
                    break;
                if (extent.second > y + 0.01 && extent.first > top + 0.01) {
                    y = extent.first;
                    moved = true;
                }
            }
        }
        if (y <= top + pageHeight * (1 - MaxBreakShift))
            y = top + pageHeight; // something taller than a page: cut it rather than loop
        breaks.append(y);
        top = y;
    }
    breaks.append(total);
    return breaks;
}

void printDocument(QPrinter *printer, const Document &doc, ImageSource *images)
{
    // Lay out in CSS-like pixels (96 per inch) and scale to the device.
    const qreal scale = printer->resolution() / CssPixelsPerInch;
    const QRectF page = printer->pageRect(QPrinter::DevicePixel);
    const qreal width = page.width() / scale;
    const qreal height = page.height() / scale - FooterHeight;
    // A printer without a resolution, or paper too small to hold the footer,
    // leaves nothing to draw on; nothing is printed rather than a division by
    // zero or an endless run of empty pages.
    if (scale <= 0 || width <= 0 || height <= 0)
        return;

    QPainter p;
    if (!p.begin(printer))
        return;

    const Theme theme = Theme::forPrint(PrintBodyPx);
    CodeHighlighter highlighter;
    highlighter.setDark(false);
    LayoutOptions options;
    options.width = width;
    options.wrapCode = true;
    options.expandAllDetails = true;
    const Layout layout(doc, theme, options, &highlighter, images);
    const QList<qreal> breaks = paginate(layout, height);

    PaintState state;
    state.images = images;
    state.preview = true;
    QFont footerFont = theme.body;
    footerFont.setPixelSize(FooterFontPx);

    p.scale(scale, scale);
    qreal top = 0;
    for (int i = 0; i < breaks.size(); ++i) {
        if (i > 0)
            printer->newPage();
        const qreal bottom = breaks[i];
        p.save();
        p.setClipRect(QRectF(0, 0, width, bottom - top));
        p.translate(0, -top);
        layout.paint(p, QRectF(0, top, width, bottom - top), state);
        p.restore();

        p.setFont(footerFont);
        p.setPen(theme.muted);
        p.drawText(QRectF(0, height + FooterGap, width, FooterHeight - FooterGap), Qt::AlignHCenter | Qt::AlignBottom,
                   QStringLiteral("%1 / %2").arg(i + 1).arg(breaks.size()));
        top = bottom;
    }
    p.end();
}

} // namespace md
