#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// JS8 message parsing and @ command handling

#include <QObject>
#include <QString>
#include <QDateTime>
#include "imodem.h"

// Parsed / decoded JS8 message ready for display and handling.
struct JS8Message {
    QDateTime  utc;
    float      audioFreqHz  = 0.0f;
    int        snrDb        = 0;
    QString    submodeStr;             // "Normal", "Fast", etc.
    int        submodeEnum  = 0;

    // Parsed fields (may be empty if raw payload couldn't be parsed)
    QString    from;                   // sender callsign
    QString    to;                     // destination: callsign, @HB, @ALL, etc.
    QString    body;                   // message body / content
    QString    rawText;                // full decoded text as seen on air
    QString    grid;                   // sender's Maidenhead locator (if found)
    double     distKm    = -1.0;       // great-circle distance from my grid (-1 = unknown)
    double     bearingDeg = -1.0;      // bearing from my grid in degrees (-1 = unknown)

    // Message-level checksum (CRC-16/KERMIT, 3 base-41 chars)
    bool       hasChecksum   = false;  // CRC suffix was detected in the message
    bool       checksumValid = false;  // CRC matched (only meaningful if hasChecksum)

    // Grid origin
    bool       gridFromCache = false;  // grid was recalled from persistent cache, not heard live

    // Command classification
    enum class Type {
        Unknown,
        Heartbeat,          // @HB
        DirectedMessage,    // DEST ORIGIN FREE_TEXT
        SnrQuery,           // DEST ORIGIN @SNR?
        SnrReply,           // DEST SNR +xx dB
        InfoQuery,          // DEST ORIGIN @INFO?
        InfoReply,          // DEST INFO text
        StatusQuery,        // DEST ORIGIN @? / @STATUS?
        StatusReply,        // DEST HEARD / DEST STATUS text
        GridQuery,          // DEST ORIGIN @GRID?
        GridReply,          // DEST GRID locator
        HearingQuery,       // DEST ORIGIN @HEARING?
        HearingReply,       // DEST HEARING callsigns
        AckMessage,         // DEST ORIGIN: ACK
        MsgCommand,         // DEST ORIGIN: MSG text  (store-and-forward message)
        QueryMsgs,          // DEST ORIGIN: QUERY MSGS  (ask if messages waiting)
        QueryMsg,           // DEST ORIGIN: QUERY MSG <id>  (retrieve specific message)
        MsgAvailable,       // MYCALL RELAY: YES MSG ID <n>  (relay has messages)
        MsgNotAvailable,    // MYCALL RELAY: NO
        MsgDelivery,        // MYCALL RELAY: MSG text FROM sender [NEXT MSG ID n]
        CompoundDirected,   // compound/group call
    };
    Type type = Type::Unknown;

    bool isAddressedToMe(const QString &mycall) const;
};

// Parses a ModemDecoded struct (from the modem interface) and the raw text
// from DecodedText into a JS8Message.
JS8Message parseDecoded(const ModemDecoded &d,
                        const QString &rawText,
                        const QString &mycall);

// Returns the submode name string. modemType: 0=Gfsk8, 1=Codec2.
QString submodeName(int submodeEnum, int modemType = 0);

// Extract a Maidenhead grid locator from arbitrary text (returns empty if not found).
QString extractGrid(const QString &text);

// Convert a 4 or 6-char Maidenhead locator to centre lat/lon.
bool gridToLatLon(const QString &grid, double &lat, double &lon);

// Calculate great-circle distance (km) and bearing (°) between two grids.
// Returns false if either grid is invalid.
bool calcDistBearing(const QString &myGrid, const QString &theirGrid,
                     double &distKm, double &bearingDeg);
