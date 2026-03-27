// SPDX-License-Identifier: GPL-3.0-or-later
#include "checksum.h"

namespace JF8Checksum {

// Base-41 alphabet matching JS8Call's Varicode::pack16bits
static const char kAlphabet[42] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-./?";
static constexpr int kBase = 41;

uint16_t crc16(const QString &text)
{
    const QByteArray bytes = text.toLocal8Bit();
    uint16_t crc = 0x0000;
    for (unsigned char c : bytes) {
        crc ^= static_cast<uint16_t>(c);
        for (int j = 0; j < 8; ++j) {
            if (crc & 1u)
                crc = static_cast<uint16_t>((crc >> 1) ^ 0x8408u); // reflected 0x1021
            else
                crc = static_cast<uint16_t>(crc >> 1);
        }
    }
    return crc;
}

QString pack16(uint16_t crc)
{
    const int a = crc / (kBase * kBase);
    const int b = (crc / kBase) % kBase;
    const int c = crc % kBase;
    return QString() + QLatin1Char(kAlphabet[a])
                     + QLatin1Char(kAlphabet[b])
                     + QLatin1Char(kAlphabet[c]);
}

bool isChecksumChars(const QString &s)
{
    if (s.length() != 3) return false;
    for (const QChar &ch : s) {
        bool found = false;
        for (int i = 0; i < kBase; ++i) {
            if (QLatin1Char(kAlphabet[i]) == ch) { found = true; break; }
        }
        if (!found) return false;
    }
    return true;
}

bool verify16(const QString &checksum, const QString &text)
{
    return pack16(crc16(text)) == checksum;
}

QString appendChecksum(const QString &text)
{
    return text + QLatin1Char(' ') + pack16(crc16(text));
}

bool tryStrip(QString &text, bool &valid)
{
    // Need at least " XXX" (4 chars) appended to a non-empty base text
    if (text.length() < 5) return false;
    const QString suffix = text.right(3);
    if (text.at(text.length() - 4) != QLatin1Char(' ')) return false;
    if (!isChecksumChars(suffix)) return false;

    const QString base = text.left(text.length() - 4);
    valid = verify16(suffix, base);
    text  = base;
    return true;
}

} // namespace JF8Checksum
