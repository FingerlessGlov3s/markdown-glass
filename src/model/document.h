#pragma once

#include <QFlags>
#include <QList>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace md {

// A run of inline text sharing the same formatting.
struct Span {
    enum Flag : quint16 {
        Bold = 1 << 0,
        Italic = 1 << 1,
        Strike = 1 << 2,
        Code = 1 << 3,
        Kbd = 1 << 4,
        Sub = 1 << 5,
        Sup = 1 << 6,
    };
    Q_DECLARE_FLAGS(Flags, Flag)
    int start = 0;
    int length = 0;
    Flags flags;
    int link = -1; // index into InlineText::links, or -1
};
Q_DECLARE_OPERATORS_FOR_FLAGS(Span::Flags)

// An image embedded in inline text; occupies one placeholder character at `pos`.
struct InlineImage {
    int pos = 0;
    QString src;
    QString alt;
    int width = -1; // explicit size from <img width=...>, or -1
    int height = -1;
    int link = -1;
};

// Inline content of a block: plain text plus formatting spans covering all of it.
struct InlineText {
    static constexpr QChar ImagePlaceholder = QChar(0xFFFC);

    QString text;
    QList<Span> spans;
    QStringList links;
    QList<InlineImage> images;

    bool isEmpty() const { return text.isEmpty(); }
};

enum class Align : quint8 { Default, Left, Center, Right };

// A list item's check box, if it has one.
enum class Task : quint8 { None, Open, Done };

struct Block {
    enum Type { Paragraph, Heading, CodeBlock, Quote, List, ListItem, Table, Rule, Details, Footnote };

    explicit Block(Type t)
        : type(t)
    { }

    Type type;
    int sourceLine = 0;
    Align align = Align::Default;

    InlineText inl; // Paragraph, Heading, Details (summary)

    int level = 0;  // Heading
    QString anchor; // Heading slug, Footnote anchor

    QString code;     // CodeBlock
    QString language; // CodeBlock: first word of the info string, empty if none

    bool ordered = false;   // List
    int listStart = 1;      // List: the number of the first item
    bool tight = false;     // List
    Task task = Task::None; // ListItem

    bool open = false; // Details
    int number = 0;    // Footnote

    // Table: rows[0] is the header row.
    QList<Align> columns;
    QList<QList<InlineText>> rows;

    std::vector<std::unique_ptr<Block>> children;
};

struct OutlineEntry {
    int level = 1;
    QString title;
    const Block *block = nullptr;
};

struct Document {
    std::vector<std::unique_ptr<Block>> blocks;
    QList<OutlineEntry> outline;

    const Block *findAnchor(const QString &anchor) const;
};

// The lines of a code block. Layout and syntax highlighting both use this, so
// highlighted line N always lines up with displayed line N.
QList<QStringView> codeLines(const QString &code);

// The anchor of footnote number n ("fn-1"), the target of its references.
QString footnoteAnchor(int number);

// GitHub's heading anchor algorithm: lowercase, drop punctuation, spaces to hyphens.
QString slugify(const QString &title);

bool isRemoteUrl(const QString &url);

} // namespace md
