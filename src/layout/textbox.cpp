#include "layout/layoutinternal.h"

// TextBox: one laid-out run of text (paragraph, heading, table cell or code
// line), with its line breaking, cursor mapping and copy text.

namespace md {

namespace {

// Qt's wrap-at-word-boundary-or-anywhere mode rescans a word that does not
// fit on a line, so its cost grows with the square of the longest word: a
// million-character word takes twenty seconds, and a file can hold far
// longer ones. Text with a word this long wraps anywhere instead, which is
// linear and costs nothing in legibility that the word had not cost already.
constexpr qsizetype LongWordChars = 2000;

bool hasLongWord(const QString &text)
{
    qsizetype run = 0;
    for (const QChar c : text) {
        run = c.isSpace() ? 0 : run + 1;
        if (run > LongWordChars)
            return true;
    }
    return false;
}

} // namespace

void TextBox::doLayout(qreal availableWidth, QTextOption::WrapMode mode, qreal imageMaxWidth)
{
    const bool wrap = mode != QTextOption::NoWrap;
    width = availableWidth;
    if (imageMaxWidth < 0)
        imageMaxWidth = availableWidth;
    if (mode == QTextOption::WrapAtWordBoundaryOrAnywhere && hasLongWord(layout.text()))
        mode = QTextOption::WrapAnywhere;

    QTextOption option;
    option.setWrapMode(mode);
    option.setAlignment(wrap ? m_align : Qt::AlignLeft);
    if (m_tabStop > 0)
        option.setTabStopDistance(m_tabStop);
    layout.setTextOption(option);

    if (!images.isEmpty())
        applyImageSpacing(imageMaxWidth);

    m_lineTops.clear();
    naturalWidth = 0;
    qreal y = 0;
    layout.beginLayout();
    for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine()) {
        line.setLineWidth(wrap ? availableWidth : Unbounded);
        const int lineStart = line.textStart();
        const int lineEnd = lineStart + line.textLength();
        qreal imageHeight = 0;
        for (const ImageSlot &slot : std::as_const(images)) {
            if (slot.image->pos >= lineStart && slot.image->pos < lineEnd)
                imageHeight = qMax(imageHeight, slot.size.height());
        }
        const qreal ascent = qMax(line.ascent(), imageHeight);
        const qreal box = qMax(m_lineHeight, ascent + line.descent());
        const qreal top = y + (box - ascent - line.descent()) / 2;
        line.setPosition(QPointF(0, top + ascent - line.ascent()));
        m_lineTops.append(y);
        y += box;
        naturalWidth = qMax(naturalWidth, line.naturalTextWidth());
    }
    layout.endLayout();
    if (m_lineTops.isEmpty()) {
        m_lineTops.append(0);
        y = m_lineHeight;
    }
    m_lineTops.append(y);
    height = y;

    for (ImageSlot &slot : images) {
        const QTextLine line = layout.lineForTextPosition(slot.image->pos);
        if (!line.isValid())
            continue;
        const qreal x = line.cursorToX(slot.image->pos);
        const qreal baseline = line.y() + line.ascent();
        slot.rect = QRectF(x, baseline - slot.size.height(), slot.size.width(), slot.size.height());
    }
}

// QTextLayout has no inline objects outside a QTextDocument, so each image is
// a placeholder glyph stretched to the image's width with letter spacing; its
// height is reserved when the lines are positioned.
void TextBox::applyImageSpacing(qreal imageMaxWidth)
{
    if (images.first().glyphAdvance < 0) {
        // Measure the placeholder glyph once, unstretched.
        layout.setFormats(m_baseFormats);
        layout.beginLayout();
        for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine())
            line.setLineWidth(Unbounded);
        layout.endLayout();
        for (ImageSlot &slot : images) {
            const QTextLine line = layout.lineForTextPosition(slot.image->pos);
            slot.glyphAdvance =
                line.isValid() ? qAbs(line.cursorToX(slot.image->pos + 1) - line.cursorToX(slot.image->pos)) : 0;
        }
    }
    QList<QTextLayout::FormatRange> formats = m_baseFormats;
    for (ImageSlot &slot : images) {
        slot.size = slot.natural;
        if (slot.size.width() > imageMaxWidth && slot.size.width() > 0)
            slot.size *= imageMaxWidth / slot.size.width();
        QTextLayout::FormatRange r;
        r.start = slot.image->pos;
        r.length = 1;
        r.format.setForeground(Qt::transparent);
        r.format.setFontLetterSpacingType(QFont::AbsoluteSpacing);
        r.format.setFontLetterSpacing(slot.size.width() - slot.glyphAdvance);
        formats.append(r);
    }
    layout.setFormats(formats);
}

namespace {

int lineIndexAt(const QList<qreal> &tops, qreal y)
{
    const int lines = tops.size() - 1;
    for (int i = 0; i < lines; ++i) {
        if (y < tops[i + 1])
            return i;
    }
    return qMax(0, lines - 1);
}

} // namespace

int TextBox::cursorAt(const QPointF &local) const
{
    if (layout.lineCount() == 0)
        return 0;
    if (local.y() < 0)
        return 0;
    if (local.y() >= height)
        return layout.text().size();
    const QTextLine line = layout.lineAt(qMin(lineIndexAt(m_lineTops, local.y()), layout.lineCount() - 1));
    return line.xToCursor(local.x());
}

int TextBox::spanAt(const QPointF &local) const
{
    if (!inl || layout.lineCount() == 0 || local.y() < 0 || local.y() >= height)
        return -1;
    const QTextLine line = layout.lineAt(qMin(lineIndexAt(m_lineTops, local.y()), layout.lineCount() - 1));
    const QRectF used = line.naturalTextRect();
    if (local.x() < used.left() || local.x() >= used.right())
        return -1;
    const int cursor = line.xToCursor(local.x(), QTextLine::CursorOnCharacter);
    for (int i = 0; i < inl->spans.size(); ++i) {
        const Span &s = inl->spans[i];
        if (cursor >= s.start && cursor < s.start + s.length)
            return i;
    }
    return -1;
}

const ImageSlot *TextBox::imageAt(const QPointF &local) const
{
    for (const ImageSlot &slot : images) {
        if (slot.rect.contains(local))
            return &slot;
    }
    return nullptr;
}

QList<QRectF> TextBox::rangeRects(int start, int length, LineBox box) const
{
    const bool fullHeight = box == LineBox::FullLine;
    QList<QRectF> rects;
    const int end = start + length;
    for (int i = 0; i < layout.lineCount(); ++i) {
        const QTextLine line = layout.lineAt(i);
        const int lineStart = line.textStart();
        const int lineEnd = lineStart + line.textLength();
        const int a = qMax(start, lineStart);
        const int b = qMin(end, lineEnd);
        if (a >= b)
            continue;
        const qreal x1 = line.cursorToX(a);
        const qreal x2 = line.cursorToX(b);
        const qreal top = fullHeight ? m_lineTops.value(i) : line.y();
        const qreal bottom = fullHeight ? m_lineTops.value(i + 1) : line.y() + line.height();
        rects.append(QRectF(qMin(x1, x2), top, qAbs(x2 - x1), bottom - top));
    }
    return rects;
}

QString TextBox::copyText(int start, int length) const
{
    QString out = layout.text().mid(start, length);
    out.replace(QChar::LineSeparator, u'\n');
    if (inl && !inl->images.isEmpty()) {
        // Replace image placeholders with their alt text, last first so offsets stay valid.
        for (int i = inl->images.size() - 1; i >= 0; --i) {
            const InlineImage &img = inl->images[i];
            if (img.pos >= start && img.pos < start + length)
                out.replace(img.pos - start, 1, img.alt);
        }
    }
    return out;
}

QList<QPair<qreal, qreal>> TextBox::lineExtents() const
{
    QList<QPair<qreal, qreal>> out;
    for (int i = 0; i + 1 < m_lineTops.size(); ++i)
        out.append({m_lineTops[i], m_lineTops[i + 1]});
    return out;
}

} // namespace md
