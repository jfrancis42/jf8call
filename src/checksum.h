#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// CRC-16/KERMIT message-level checksum, JS8Call-compatible.
//
// Algorithm : CRC-16/KERMIT (poly=0x1021, init=0, final_xor=0,
//             reflect_in=true, reflect_out=true).
// Encoding  : 3 base-41 characters using the alphabet
//             "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-./?".
//
// Usage for transmit:
//   QString txText = JF8Checksum::appendChecksum(directedText);
//
// Usage for receive:
//   QString body = parsedBody;
//   msg.hasChecksum = JF8Checksum::tryStrip(body, msg.checksumValid);
//   // body now has the checksum suffix removed

#include <QString>
#include <cstdint>

namespace JF8Checksum {

// Compute CRC-16/KERMIT over the local-8-bit representation of text.
uint16_t crc16(const QString &text);

// Encode a 16-bit value as 3 base-41 characters.
QString  pack16(uint16_t crc);

// Return true if all three characters of s belong to the base-41 alphabet.
bool     isChecksumChars(const QString &s);

// Verify that the 3-char checksum matches crc16(text).
bool     verify16(const QString &checksum, const QString &text);

// Return text + ' ' + pack16(crc16(text)).
QString  appendChecksum(const QString &text);

// If text ends with " XXX" where XXX is 3 base-41 chars, strip the suffix,
// set valid = (CRC matches), and return true.
// If no checksum-shaped suffix is found, leave text unchanged and return false.
bool     tryStrip(QString &text, bool &valid);

} // namespace JF8Checksum
