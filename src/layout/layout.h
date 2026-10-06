#pragma once

#include "layout/imagesource.h"
#include "layout/theme.h"
#include "model/document.h"

#include <QFont>
#include <QList>
#include <QRectF>
#include <QSet>
#include <QTextLayout>

#include <memory>
#include <vector>

class QPainter;

namespace md {

class CodeHighlighter;

struct ImageSlot {
    const InlineImage *image = nullptr;
    QSizeF natural;          // wanted size before fitting to the available width
    QSizeF size;             // laid-out size
    QRectF rect;             // relative to the owning TextBox
    qreal glyphAdvance = -1; // measured width of the placeholder glyph
    bool ready = false;
};

// One laid-out piece of text: a paragraph, heading, table cell or a single code line.
// What rangeRects() measures: the glyphs alone, or the whole line box
// including the leading above and below them.
enum class LineBox { Glyphs, FullLine };

class TextBox
{
public:
    QTextLayout layout;
    QPointF pos; // document coordinates
    qreal width = 0;
    qreal height = 0;
    qreal naturalWidth = 0;
    int frame = -1; // index into Layout::frames if horizontally scrollable
    const Block *block = nullptr;
    const InlineText *inl = nullptr; // null for code lines
    bool isCode = false;
    QString separator = QStringLiteral("\n"); // follows this box's text when copying
    QList<ImageSlot> images;

    QRectF rect() const { return QRectF(pos, QSizeF(width, height)); }

    // (Re)computes line breaks for the given width. Images are scaled down to
    // imageMaxWidth (the available width if negative).
    void doLayout(qreal availableWidth, QTextOption::WrapMode mode, qreal imageMaxWidth = -1);

    // Text offset nearest to a point given relative to pos.
    int cursorAt(const QPointF &local) const;
    // Index into inl->spans of the span under the point, or -1.
    int spanAt(const QPointF &local) const;
    const ImageSlot *imageAt(const QPointF &local) const;
    // Rectangles (relative to pos) covering a text range.
    QList<QRectF> rangeRects(int start, int length, LineBox box) const;
    QString copyText(int start, int length) const;

    // Unbreakable vertical extents (relative to pos), used for pagination.
    QList<QPair<qreal, qreal>> lineExtents() const;

private:
    friend class LayoutBuilder;
    void applyImageSpacing(qreal imageMaxWidth);

    qreal m_lineHeight = 0;
    qreal m_tabStop = 0;
    Qt::Alignment m_align = Qt::AlignLeft;
    QList<QTextLayout::FormatRange> m_baseFormats;
    QList<qreal> m_lineTops; // size = lineCount + 1
};

struct Deco {
    enum Kind { Fill, RoundFill, Disc, Circle, Square, Checkbox, Text, Triangle };
    Kind kind = Fill;
    QRectF rect;
    QColor color;
    QString text;    // Text
    QFont font;      // Text
    bool on = false; // Checkbox checked, Triangle open
    int frame = -1;
};

// A horizontally scrollable region: an unwrapped code block or an over-wide table.
struct ScrollFrame {
    QRectF rect;
    qreal contentWidth = 0;
    qreal offset = 0;
    const Block *block = nullptr;

    qreal maxOffset() const { return qMax(0.0, contentWidth - rect.width()); }
};

struct CodeArea {
    QRectF rect;
    const Block *block = nullptr;
};

struct ToggleArea {
    QRectF rect;
    const Block *block = nullptr;
};

struct LayoutOptions {
    qreal width = 800;
    bool wrapCode = true;
    // Details blocks whose open state the user flipped from the document's default.
    const QSet<const Block *> *toggledDetails = nullptr;
    bool expandAllDetails = false; // printing: paper cannot be clicked
};

struct FindMatch {
    int box = 0;
    int start = 0;
    int length = 0;
};

struct PaintState {
    // Normalised selection: (startBox, startPos) <= (endBox, endPos); startBox < 0 for none.
    int selStartBox = -1;
    int selStartPos = 0;
    int selEndBox = -1;
    int selEndPos = 0;
    int hoverBox = -1;
    int hoverLink = -1;
    const std::vector<FindMatch> *matches = nullptr; // sorted by box
    int currentMatch = -1;
    ImageSource *images = nullptr;
    // A minimap tile, a printed page or a PNG: nobody is looking at the
    // images there, so painting them must not keep animations running.
    bool preview = false;
};

struct Hit {
    int box = -1;
    int pos = 0;
    bool inside = false; // the point is within the box, not merely nearest to it
    int span = -1;       // index into box.inl->spans; only ever set for boxes with inline text
    const ImageSlot *image = nullptr;

    // Index into box.inl->links of the link under the point, or -1.
    int link(const TextBox &b) const;
    // The target of the link under the point, or an empty string.
    QString href(const TextBox &b) const;
    // The formatting span under the point, or null.
    const Span *inlineSpan(const TextBox &b) const;
};

class Layout
{
public:
    Layout(const Document &doc, const Theme &theme, const LayoutOptions &options, CodeHighlighter *highlighter,
           ImageSource *images);

    qreal width() const { return m_width; }
    qreal height() const { return m_height; }
    const Theme &theme() const { return m_theme; }

    void paint(QPainter &painter, const QRectF &clip, const PaintState &state) const;
    Hit hitTest(const QPointF &point) const;
    // Maps a box-local rectangle to document coordinates, applying frame scrolling.
    QRectF toDocument(const TextBox &box, const QRectF &local) const;
    qreal blockTop(const Block *block) const { return m_blockTops.value(block, -1); }
    // The block whose top is nearest above y, for keeping place across re-layouts.
    const Block *blockAt(qreal y) const;
    // The first block starting at or after a source line, for restoring the
    // position after the file is reloaded.
    const Block *blockForLine(int line) const;

    std::vector<std::unique_ptr<TextBox>> texts;
    std::vector<Deco> decos;
    std::vector<ScrollFrame> frames;
    std::vector<CodeArea> codeAreas;
    std::vector<ToggleArea> toggles;
    // Extents a page break must not fall inside, beyond individual text lines:
    // table rows, and headings together with the start of what follows them.
    QList<QPair<qreal, qreal>> unbreakable;

private:
    friend class LayoutBuilder;
    void paintDeco(QPainter &p, const Deco &d) const;
    void paintText(QPainter &p, int index, const PaintState &state) const;

    Theme m_theme;
    qreal m_width = 0;
    qreal m_height = 0;
    QHash<const Block *, qreal> m_blockTops;
    QList<QPair<qreal, const Block *>> m_blockOrder;
};

} // namespace md
