#include "model/textcheck.h"

#include <QStringDecoder>

namespace md {

namespace {
constexpr qsizetype SampleBytes = qsizetype(64) * 1024;
// Tolerances, so a real text file with a stray odd byte, a cut-off multi-byte
// character at the end of the sample, or an older 8-bit encoding (a few
// percent of accented letters) still counts as text. Binary data decoded as
// UTF-8 is mostly invalid, far above these.
constexpr double MaxControlShare = 0.01;
constexpr double MaxInvalidShare = 0.10;
constexpr int Slack = 4;
} // namespace

NotText checkText(QByteArrayView data)
{
    QByteArrayView sample = data.first(qMin(data.size(), SampleBytes));
    if (sample.startsWith(Utf8Bom))
        sample = sample.sliced(Utf8Bom.size());
    if (sample.isEmpty())
        return NotText::No;
    if (sample.contains('\0'))
        return NotText::NulBytes;

    qsizetype control = 0;
    for (const char byte : sample) {
        const uchar c = uchar(byte);
        // Tab, newline, form feed, carriage return and escape (terminal colour
        // codes) are fine; other C0 controls and DEL are not written text.
        const bool allowed = c == '\t' || c == '\n' || c == '\f' || c == '\r' || c == 0x1B;
        if ((c < 0x20 && !allowed) || c == 0x7F)
            ++control;
    }
    if (control > Slack && control > sample.size() * MaxControlShare)
        return NotText::ControlCharacters;

    // Each invalid UTF-8 sequence decodes to U+FFFD; genuine U+FFFD characters
    // in the file are subtracted so they are not counted against it.
    const QString decoded = QStringDecoder(QStringDecoder::Utf8)(sample);
    const qsizetype replacements = decoded.count(QChar::ReplacementCharacter) - sample.count("\xEF\xBF\xBD");
    if (replacements > Slack && replacements > decoded.size() * MaxInvalidShare)
        return NotText::InvalidUtf8;

    return NotText::No;
}

} // namespace md
