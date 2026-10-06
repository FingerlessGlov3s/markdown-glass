#pragma once

// The conversion from cmark-gfm's syntax tree to a Document, shared by
// parser.cpp and htmlblockreader.cpp. Not part of the model API: include
// only from src/model/*.cpp.

#include "model/document.h"
#include "model/inlinebuilder.h"

#include <QHash>
#include <QList>
#include <cmark-gfm.h>

#include <memory>
#include <vector>

namespace md {

using BlockList = std::vector<std::unique_ptr<Block>>;

class Converter
{
public:
    explicit Converter(Document &doc)
        : m_doc(doc)
    { }

    void convertChildren(cmark_node *parent, BlockList &out, Align inherited);

private:
    struct Scope {
        QList<BlockList *> targets;
        QList<Block *> details;
        QList<Align> aligns;

        BlockList &target() { return *targets.last(); }
        Align align() const { return aligns.last(); }
    };

    void inlines(cmark_node *parent, InlineBuilder &b);
    class HtmlBlockReader; // htmlblockreader.cpp
    void htmlBlock(const QString &html, Scope &scope, int line);
    void table(cmark_node *node, Block &block);
    void finishHeading(Block &block);
    static QString plainText(cmark_node *node, int depth = 0);

    // Hostile input can nest arbitrarily deep; content past this depth is dropped
    // so recursion here and in layout stays bounded.
    static constexpr int MaxDepth = 64;
    // cmark-gfm accepts tables of up to 65535 columns, and every row is padded
    // to the header's width, so a few hundred kilobytes of markdown could ask
    // for billions of cells. Columns past the limit and rows past the cell
    // budget are dropped.
    static constexpr int MaxTableColumns = 128;
    static constexpr qsizetype MaxTableCells = 100'000;

    Document &m_doc;
    QHash<QString, int> m_slugCounts;
    int m_footnotes = 0;
    int m_blockDepth = 0;
    int m_openDetails = 0;
    int m_inlineDepth = 0;
};

} // namespace md
