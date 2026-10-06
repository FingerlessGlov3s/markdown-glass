#include "model/inlinebuilder.h"

namespace md {

namespace {

bool applyInlineFlagTag(InlineBuilder &b, const QString &name, bool open)
{
    static const QHash<QString, Span::Flag> map = {
        {QStringLiteral("b"), Span::Bold},        {QStringLiteral("strong"), Span::Bold},
        {QStringLiteral("i"), Span::Italic},      {QStringLiteral("em"), Span::Italic},
        {QStringLiteral("code"), Span::Code},     {QStringLiteral("tt"), Span::Code},
        {QStringLiteral("samp"), Span::Code},     {QStringLiteral("kbd"), Span::Kbd},
        {QStringLiteral("sub"), Span::Sub},       {QStringLiteral("sup"), Span::Sup},
        {QStringLiteral("del"), Span::Strike},    {QStringLiteral("s"), Span::Strike},
        {QStringLiteral("strike"), Span::Strike},
    };
    const auto it = map.constFind(name);
    if (it == map.constEnd())
        return false;
    b.setFlag(it.value(), open);
    return true;
}

} // namespace

Align parseAlign(const QString &value, Align fallback)
{
    const QString v = value.trimmed().toLower();
    if (v == QLatin1String("center") || v == QLatin1String("middle"))
        return Align::Center;
    if (v == QLatin1String("right"))
        return Align::Right;
    if (v == QLatin1String("left"))
        return Align::Left;
    return fallback;
}

int parseDimension(const QString &value)
{
    // Pixel values only; percentages and other units fall back to the natural size.
    QString v = value.trimmed();
    if (v.endsWith(QLatin1String("px"), Qt::CaseInsensitive))
        v.chop(2);
    bool ok = false;
    const int n = v.toInt(&ok);
    return ok && n > 0 && n <= MaxImageDimension ? n : -1;
}

void applyInlineTag(InlineBuilder &b, const HtmlToken &t)
{
    const bool open = t.kind == HtmlToken::Open;
    if (applyInlineFlagTag(b, t.name, open))
        return;
    if (t.name == QLatin1String("br")) {
        if (open)
            b.lineBreak();
        return;
    }
    if (t.name == QLatin1String("img")) {
        if (open && !t.attrs.value(QStringLiteral("src")).isEmpty()) {
            b.appendImage(t.attrs.value(QStringLiteral("src")), t.attrs.value(QStringLiteral("alt")),
                          parseDimension(t.attrs.value(QStringLiteral("width"))),
                          parseDimension(t.attrs.value(QStringLiteral("height"))));
        }
        return;
    }
    if (t.name == QLatin1String("a")) {
        if (open)
            b.openHtmlAnchor(t.attrs.value(QStringLiteral("href")));
        else
            b.closeHtmlAnchor();
    }
    // Any other tag is not supported inline and is dropped; its text is kept.
}

} // namespace md
