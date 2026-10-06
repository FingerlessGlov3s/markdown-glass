#include "layout/theme.h"

#include "layout/layoutinternal.h"

#include <QFontDatabase>
#include <QFontInfo>
#include <QGuiApplication>

namespace md {

QColor mixColors(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

namespace {

// The desktop UI font is sized for controls; reading text is set a step
// larger, which lands on GitHub's 16px with a default 10pt system font.
constexpr qreal ReadingScale = 1.2;
constexpr qreal MinBodyPx = 6;
constexpr qreal MaxBodyPx = 96;

void deriveColors(Theme &t)
{
    t.dark = t.background.lightnessF() < 0.5;
    t.muted = mixColors(t.text, t.background, 0.35);
    t.border = mixColors(t.background, t.text, t.dark ? 0.22 : 0.16);
    t.codeBackground = mixColors(t.background, t.text, t.dark ? 0.09 : 0.05);
    t.inlineCodeBackground = mixColors(t.background, t.text, t.dark ? 0.16 : 0.10);
    t.tableAltBackground = mixColors(t.background, t.text, 0.04);
    t.findMatch = t.dark ? QColor(120, 100, 0) : QColor(255, 235, 120);
    t.findCurrent = t.dark ? QColor(180, 110, 0) : QColor(255, 170, 60);
    t.success = t.dark ? QColor(0x56, 0xd3, 0x64) : QColor(0x1a, 0x7f, 0x37);
}

void setFonts(Theme &t, qreal bodyPx)
{
    t.px = bodyPx;
    t.body = QGuiApplication::font();
    t.body.setPixelSize(qRound(bodyPx));
    t.mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    t.mono.setPixelSize(qRound(bodyPx * Metrics::SmallText));
}

} // namespace

Theme Theme::fromPalette(const QPalette &palette, qreal zoom)
{
    Theme t;
    const qreal uiPx = QFontInfo(QGuiApplication::font()).pixelSize();
    setFonts(t, qBound(MinBodyPx, uiPx * ReadingScale * zoom, MaxBodyPx));

    t.background = palette.color(QPalette::Base);
    t.text = palette.color(QPalette::Text);
    t.link = palette.color(QPalette::Link);
    t.selection = palette.color(QPalette::Highlight);
    t.selectionText = palette.color(QPalette::HighlightedText);
    deriveColors(t);
    return t;
}

Theme Theme::forPrint(qreal bodyPx)
{
    Theme t;
    setFonts(t, bodyPx);
    t.background = Qt::white;
    t.text = QColor(0x1f, 0x23, 0x28);
    t.link = QColor(0x09, 0x69, 0xda);
    t.selection = Qt::transparent;
    t.selectionText = t.text;
    deriveColors(t);
    return t;
}

} // namespace md
