#include "tes3_encoding.h"

#include <algorithm>
#include <array>

namespace tes3_encoding {
namespace {

// CP1252 bytes 0x80-0x9F -> Unicode. 0x0000 marks the five bytes CP1252
// leaves undefined; those decode to U+FFFD.
constexpr std::array<char16_t, 32> kHiBytes = {
    0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, // 80-87
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x017D, 0x0000, // 88-8F
    0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, // 90-97
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178, // 98-9F
};

} // namespace

QString fromCp1252(const char *data, qsizetype size)
{
    if (size <= 0 || data == nullptr) return {};

    QString out;
    out.reserve(size);
    for (qsizetype i = 0; i < size; ++i) {
        const auto b = static_cast<unsigned char>(data[i]);
        if (b < 0x80 || b >= 0xA0) {
            out.append(QChar(b));
        } else {
            const char16_t mapped = kHiBytes[b - 0x80];
            out.append(mapped ? QChar(mapped) : QChar(QChar::ReplacementCharacter));
        }
    }
    return out;
}

QByteArray toCp1252(const QString &str)
{
    QByteArray out;
    out.reserve(str.size());
    for (const QChar c : str) {
        const char16_t u = c.unicode();
        if (u < 0x80 || (u >= 0xA0 && u <= 0xFF)) {
            out.append(static_cast<char>(u));
            continue;
        }
        const auto it = std::ranges::find(kHiBytes, u);
        out.append(it != kHiBytes.end()
                       ? static_cast<char>(0x80 + (it - kHiBytes.begin()))
                       : '?');
    }
    return out;
}

} // namespace tes3_encoding
