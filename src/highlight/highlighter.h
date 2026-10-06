#pragma once

#include <QHash>
#include <QList>
#include <QRgb>
#include <QString>

#include <memory>

namespace md {

struct Block;

struct HighlightRange {
    int start = 0;
    int length = 0;
    QRgb color = 0;
    bool hasColor = false;
    bool bold = false;
    bool italic = false;
    bool underline = false;
};

using HighlightedLines = QList<QList<HighlightRange>>;

// Syntax-highlights fenced code blocks with KSyntaxHighlighting and caches the
// result per block. A block with no language, or an unknown one, is left plain.
class CodeHighlighter
{
public:
    CodeHighlighter();
    ~CodeHighlighter();

    // Picks the highlighting theme that suits a light or dark background.
    void setDark(bool dark);

    // Returns one list of ranges per line of block.code, or nullptr for plain text.
    const HighlightedLines *highlight(const Block &block);

    void clear();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
    QHash<const Block *, HighlightedLines> m_cache;
    bool m_dark = false;
};

} // namespace md
