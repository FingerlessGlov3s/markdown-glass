#pragma once

// Helpers shared by the layout implementation files. Not part of the layout
// API: include only from src/layout/*.cpp.

#include "layout/layout.h"

#include <QFont>
#include <QString>

namespace md {

class CodeHighlighter;

// Spacing and sizes in em (multiples of the body font size), after GitHub's
// markdown stylesheet. Change the look here rather than in the layout code.
namespace Metrics {
inline constexpr qreal BodyLineHeight = 1.5;
inline constexpr qreal SmallText = 0.85; // footnotes, and inline code relative to its text
inline constexpr qreal BlockGap = 1.0;   // between paragraphs, lists, code blocks, tables
inline constexpr qreal SectionGap = 1.5; // before headings and around horizontal rules
inline constexpr qreal TightGap = 0.25;  // between the items of a tight list
inline constexpr qreal FootnoteGap = 0.5;
inline constexpr qreal RuleThickness = 0.25;
inline constexpr qreal HeadingScale[] = {2.0, 1.5, 1.25, 1.0, 0.875, 0.85}; // h1..h6
inline constexpr qreal HeadingLineHeight = 1.25;                            // relative to the heading's own size
inline constexpr qreal HeadingRuleGap = 0.3;  // under h1/h2, relative to the heading's own size
inline constexpr qreal KeepWithHeading = 4.0; // when printing, a heading keeps this much of what follows
inline constexpr qreal CodePadding = 1.0;
inline constexpr qreal CodeLineHeight = 1.45; // relative to the code font size
inline constexpr qreal MinColumn = 4.0;       // narrowest width text is ever squeezed into
inline constexpr qreal MinNestedColumn = 6.0; // ...inside quotes, lists and footnotes
inline constexpr qreal QuoteIndent = 1.0;
inline constexpr qreal QuoteBarWidth = 0.25;
inline constexpr qreal ListIndent = 2.0;
inline constexpr qreal ListNumberGap = 0.4; // between "1." and the item text
inline constexpr qreal CheckboxSize = 0.85;
inline constexpr qreal CheckboxOffset = 1.45; // left of the item text
inline constexpr qreal BulletSize = 0.36;
inline constexpr qreal BulletOffset = 1.05; // left of the item text
inline constexpr qreal TableCellPadX = 0.8;
inline constexpr qreal TableCellPadY = 0.375;
inline constexpr qreal TableScrollBarRoom = 0.8; // below a table that scrolls sideways
inline constexpr qreal SummaryIndent = 1.2;      // summary text, after the disclosure triangle
inline constexpr qreal TriangleSize = 0.55;
inline constexpr qreal TriangleInset = 0.15;
inline constexpr qreal DetailsGap = 0.75; // between an open summary and its content
inline constexpr qreal FootnoteIndent = 2.0;
inline constexpr qreal PlaceholderPadX = 1.2; // image placeholder chip
inline constexpr qreal PlaceholderPadY = 0.3;
inline constexpr qreal PlaceholderMaxWidth = 22;
inline constexpr qreal PlaceholderText = 0.8;      // its label's font size
inline constexpr qreal PlaceholderTextInset = 0.4; // its label's side margins
inline constexpr qreal CodeBlockRadius = 0.375;    // corner radius of code blocks
inline constexpr qreal InlineRadius = 0.25;        // ...of inline code, keys and placeholders
inline constexpr qreal InlineCodeOverhang = 0.2;   // inline code background beyond its text
inline constexpr qreal KbdOverhang = 0.12;         // the same for <kbd>
inline constexpr qreal EmptyLineSelection = 0.4;   // width shown for a selected empty line
// In pixels, not em: these do not scale with the font.
inline constexpr qreal HairlinePx = 1;     // rules under headings and before footnotes, table grid lines
inline constexpr int MinInlineCodePx = 6;  // inline code is scaled down from its text; never below this
inline constexpr int MinPlaceholderPx = 8; // an image placeholder's label must stay legible
inline constexpr int TabWidthSpaces = 8;   // in code blocks, as GitHub renders them
// A block only scrolls sideways when its content is wider than its column by
// more than this, so sub-pixel rounding never produces a scrollbar.
inline constexpr qreal ScrollSlackPx = 0.5;
// Table cells are measured a pixel wider than their text, so a cell laid out
// at exactly its measured width never wraps one more time.
inline constexpr qreal CellSlackPx = 1;
} // namespace Metrics

// Line width used for "do not wrap": wider than anything that will be shown.
constexpr qreal Unbounded = 1e6;

// Fills `layout` from `doc`. Implemented by LayoutBuilder (layoutbuilder.cpp).
void buildLayout(Layout &layout, const Document &doc, const LayoutOptions &options, CodeHighlighter *highlighter,
                 ImageSource *images);

// Images that are not (yet) shown appear as a small chip with their alt text.
inline QFont placeholderFont(const Theme &theme)
{
    QFont font = theme.body;
    font.setPixelSize(qMax(Metrics::MinPlaceholderPx, qRound(theme.px * Metrics::PlaceholderText)));
    return font;
}

inline QString placeholderLabel(const InlineImage &image)
{
    return image.alt.isEmpty() ? QStringLiteral("image") : image.alt;
}

} // namespace md
