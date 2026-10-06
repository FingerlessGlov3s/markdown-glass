#include "model/parser.h"

#include "model/converter.h"

#include <QScopeGuard>
#include <cmark-gfm-core-extensions.h>

#include <cstring>
#include <utility>

namespace md {

QString Converter::plainText(cmark_node *node, int depth)
{
    QString out;
    for (cmark_node *c = cmark_node_first_child(node); c; c = cmark_node_next(c)) {
        switch (cmark_node_get_type(c)) {
        case CMARK_NODE_TEXT:
        case CMARK_NODE_CODE:
            out += QString::fromUtf8(cmark_node_get_literal(c));
            break;
        case CMARK_NODE_SOFTBREAK:
        case CMARK_NODE_LINEBREAK:
            out += u' ';
            break;
        default:
            if (depth < MaxDepth)
                out += plainText(c, depth + 1);
        }
    }
    return out;
}

void Converter::inlines(cmark_node *parent, InlineBuilder &b)
{
    if (m_inlineDepth >= MaxDepth)
        return;
    ++m_inlineDepth;
    const auto leave = qScopeGuard([this] { --m_inlineDepth; });

    for (cmark_node *n = cmark_node_first_child(parent); n; n = cmark_node_next(n)) {
        switch (cmark_node_get_type(n)) {
        case CMARK_NODE_TEXT:
            b.appendText(QString::fromUtf8(cmark_node_get_literal(n)));
            break;
        case CMARK_NODE_SOFTBREAK:
            b.appendText(u" ");
            break;
        case CMARK_NODE_LINEBREAK:
            b.lineBreak();
            break;
        case CMARK_NODE_CODE:
            b.setFlag(Span::Code, true);
            b.appendText(QString::fromUtf8(cmark_node_get_literal(n)));
            b.setFlag(Span::Code, false);
            break;
        case CMARK_NODE_EMPH:
            b.setFlag(Span::Italic, true);
            inlines(n, b);
            b.setFlag(Span::Italic, false);
            break;
        case CMARK_NODE_STRONG:
            b.setFlag(Span::Bold, true);
            inlines(n, b);
            b.setFlag(Span::Bold, false);
            break;
        case CMARK_NODE_LINK:
            b.pushLink(QString::fromUtf8(cmark_node_get_url(n)));
            inlines(n, b);
            b.popLink();
            break;
        case CMARK_NODE_IMAGE:
            b.appendImage(QString::fromUtf8(cmark_node_get_url(n)), plainText(n));
            break;
        case CMARK_NODE_HTML_INLINE: {
            const QString html = QString::fromUtf8(cmark_node_get_literal(n));
            for (const HtmlToken &t : tokenizeHtml(html)) {
                if (t.kind == HtmlToken::Text)
                    b.appendText(t.text);
                else
                    applyInlineTag(b, t);
            }
            break;
        }
        case CMARK_NODE_FOOTNOTE_REFERENCE: {
            // After parsing, the literal holds the footnote's number.
            const QString number = QString::fromUtf8(cmark_node_get_literal(n));
            b.setFlag(Span::Sup, true);
            b.pushLink(u'#' + footnoteAnchor(number.toInt()));
            b.appendText(number);
            b.popLink();
            b.setFlag(Span::Sup, false);
            break;
        }
        default:
            if (std::strcmp(cmark_node_get_type_string(n), "strikethrough") == 0) {
                b.setFlag(Span::Strike, true);
                inlines(n, b);
                b.setFlag(Span::Strike, false);
            } else {
                inlines(n, b);
            }
        }
    }
}

void Converter::finishHeading(Block &block)
{
    QString title = block.inl.text;
    title.remove(InlineText::ImagePlaceholder);
    title.replace(QChar::LineSeparator, u' ');
    title = title.trimmed();

    const QString slug = slugify(title);
    const int seen = m_slugCounts.value(slug, 0);
    m_slugCounts.insert(slug, seen + 1);
    block.anchor = seen == 0 ? slug : slug + u'-' + QString::number(seen);

    if (!title.isEmpty())
        m_doc.outline.append(OutlineEntry {block.level, title, &block});
}

void Converter::table(cmark_node *node, Block &block)
{
    const int columns = qMin(int(cmark_gfm_extensions_get_table_columns(node)), MaxTableColumns);
    const uint8_t *aligns = cmark_gfm_extensions_get_table_alignments(node);
    for (int i = 0; i < columns; ++i) {
        Align a = Align::Default;
        if (aligns) {
            if (aligns[i] == 'c')
                a = Align::Center;
            else if (aligns[i] == 'r')
                a = Align::Right;
            else if (aligns[i] == 'l')
                a = Align::Left;
        }
        block.columns.append(a);
    }
    if (columns <= 0)
        return;
    const qsizetype maxRows = MaxTableCells / columns;
    for (cmark_node *row = cmark_node_first_child(node); row && block.rows.size() < maxRows;
         row = cmark_node_next(row)) {
        QList<InlineText> cells;
        for (cmark_node *cell = cmark_node_first_child(row); cell && cells.size() < columns;
             cell = cmark_node_next(cell)) {
            InlineBuilder b;
            inlines(cell, b);
            cells.append(b.take());
        }
        while (cells.size() < columns)
            cells.append(InlineText());
        cells.resize(columns);
        block.rows.append(cells);
    }
}

void Converter::convertChildren(cmark_node *parent, BlockList &out, Align inherited)
{
    if (m_blockDepth + m_openDetails >= MaxDepth)
        return;
    ++m_blockDepth;
    Scope scope;
    const auto leave = qScopeGuard([this, &scope] {
        --m_blockDepth;
        m_openDetails -= scope.details.size(); // details left unclosed in this container
    });

    scope.targets.append(&out);
    scope.aligns.append(inherited);

    for (cmark_node *n = cmark_node_first_child(parent); n; n = cmark_node_next(n)) {
        const int line = cmark_node_get_start_line(n);
        std::unique_ptr<Block> block;

        switch (cmark_node_get_type(n)) {
        case CMARK_NODE_PARAGRAPH: {
            InlineBuilder b;
            inlines(n, b);
            if (!b.hasContent())
                break;
            block = std::make_unique<Block>(Block::Paragraph);
            block->align = scope.align();
            block->inl = b.take();
            break;
        }
        case CMARK_NODE_HEADING: {
            InlineBuilder b;
            inlines(n, b);
            block = std::make_unique<Block>(Block::Heading);
            block->level = cmark_node_get_heading_level(n);
            block->align = scope.align();
            block->inl = b.take();
            finishHeading(*block);
            break;
        }
        case CMARK_NODE_CODE_BLOCK: {
            block = std::make_unique<Block>(Block::CodeBlock);
            block->code = QString::fromUtf8(cmark_node_get_literal(n));
            if (block->code.endsWith(u'\n'))
                block->code.chop(1);
            const QString info = QString::fromUtf8(cmark_node_get_fence_info(n)).trimmed();
            block->language = info.section(QChar(u' '), 0, 0).toLower();
            break;
        }
        case CMARK_NODE_BLOCK_QUOTE:
            block = std::make_unique<Block>(Block::Quote);
            convertChildren(n, block->children, Align::Default);
            break;
        case CMARK_NODE_LIST:
            block = std::make_unique<Block>(Block::List);
            block->ordered = cmark_node_get_list_type(n) == CMARK_ORDERED_LIST;
            block->listStart = cmark_node_get_list_start(n);
            block->tight = cmark_node_get_list_tight(n);
            convertChildren(n, block->children, Align::Default);
            break;
        case CMARK_NODE_ITEM:
            block = std::make_unique<Block>(Block::ListItem);
            if (std::strcmp(cmark_node_get_type_string(n), "tasklist") == 0)
                block->task = cmark_gfm_extensions_get_tasklist_item_checked(n) ? Task::Done : Task::Open;
            convertChildren(n, block->children, Align::Default);
            break;
        case CMARK_NODE_THEMATIC_BREAK:
            block = std::make_unique<Block>(Block::Rule);
            break;
        case CMARK_NODE_HTML_BLOCK:
            htmlBlock(QString::fromUtf8(cmark_node_get_literal(n)), scope, line);
            break;
        case CMARK_NODE_FOOTNOTE_DEFINITION:
            block = std::make_unique<Block>(Block::Footnote);
            block->number = ++m_footnotes;
            block->anchor = footnoteAnchor(block->number);
            convertChildren(n, block->children, Align::Default);
            break;
        default:
            if (std::strcmp(cmark_node_get_type_string(n), "table") == 0) {
                block = std::make_unique<Block>(Block::Table);
                table(n, *block);
            } else {
                // Unknown container: keep its content.
                convertChildren(n, scope.target(), scope.align());
            }
        }

        if (block) {
            block->sourceLine = line;
            scope.target().push_back(std::move(block));
        }
    }
}

QString parserVersion()
{
    return QString::fromLatin1(cmark_version_string());
}

std::unique_ptr<Document> parseMarkdown(const QByteArray &utf8)
{
    cmark_gfm_core_extensions_ensure_registered();

    const int options = CMARK_OPT_FOOTNOTES | CMARK_OPT_VALIDATE_UTF8;
    cmark_parser *parser = cmark_parser_new(options);
    for (const char *name : {"table", "strikethrough", "autolink", "tasklist"}) {
        if (cmark_syntax_extension *ext = cmark_find_syntax_extension(name))
            cmark_parser_attach_syntax_extension(parser, ext);
    }
    cmark_parser_feed(parser, utf8.constData(), size_t(utf8.size()));
    cmark_node *root = cmark_parser_finish(parser);

    auto doc = std::make_unique<Document>();
    if (root) {
        Converter converter(*doc);
        converter.convertChildren(root, doc->blocks, Align::Default);
        cmark_node_free(root);
    }
    cmark_parser_free(parser);

    return doc;
}

} // namespace md
