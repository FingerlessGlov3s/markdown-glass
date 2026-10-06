#pragma once

#include <QColor>
#include <QFont>
#include <QPalette>

namespace md {

// Colours, fonts and metrics used to lay out and paint a document. Colours are
// derived from the desktop palette; proportions follow GitHub's markdown style.
struct Theme {
    QFont body;
    QFont mono;
    qreal px = 16; // body font size in pixels; all spacing scales with it

    bool dark = false;
    QColor background;
    QColor text;
    QColor muted;
    QColor link;
    QColor border;
    QColor codeBackground;
    QColor inlineCodeBackground;
    QColor tableAltBackground;
    QColor selection;
    QColor selectionText;
    QColor findMatch;
    QColor findCurrent;
    QColor success; // confirmation marks, such as a copy button's tick

    qreal em(qreal factor) const { return px * factor; }
    // Space around the document: left and right of the text column (at least)
    // and above and below the content. Shared by the view and --render-png.
    qreal contentMargin() const { return em(2); }
    // The narrowest the text column gets, however small the window.
    qreal minContentWidth() const { return em(8); }

    static Theme fromPalette(const QPalette &palette, qreal zoom);
    // Fixed light theme for printing, independent of the desktop colours.
    static Theme forPrint(qreal bodyPx);
};

QColor mixColors(const QColor &a, const QColor &b, qreal t);

} // namespace md
