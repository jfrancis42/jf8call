// SPDX-License-Identifier: GPL-3.0-or-later
#include "messagemodel.h"
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QDateTime>
#include <algorithm>

// Row 0 is always a virtual "@ALL" entry (broadcast / deselect target).
// Real messages occupy rows 1..N (m_messages indices 0..N-1).

MessageModel::MessageModel(QObject *parent)
    : QAbstractTableModel(parent)
{
    m_ageTimer = new QTimer(this);
    m_ageTimer->setInterval(1000);
    connect(m_ageTimer, &QTimer::timeout, this, [this]() {
        if (m_messages.isEmpty()) return;

        // Expire rows older than maxAgeMins
        const qint64 maxSecs = static_cast<qint64>(m_maxAgeMins) * 60;
        const QDateTime now  = QDateTime::currentDateTimeUtc();
        for (int i = m_messages.size() - 1; i >= 0; --i) {
            if (m_messages[i].utc.secsTo(now) > maxSecs) {
                beginRemoveRows({}, i, i);
                m_messages.removeAt(i);
                endRemoveRows();
            }
        }

        if (!m_messages.isEmpty())
            emit dataChanged(index(0, ColAge), index(m_messages.size() - 1, ColAge),
                             {Qt::DisplayRole, Qt::UserRole});
    });
    m_ageTimer->start();
}

int MessageModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_messages.size();
}

int MessageModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant MessageModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return {};

    const int msgIdx = index.row();
    if (msgIdx >= m_messages.size()) return {};
    const JS8Message &msg = m_messages.at(msgIdx);

    if (role == Qt::DisplayRole || role == Qt::UserRole) {
        const qint64 ageSecs = msg.utc.secsTo(QDateTime::currentDateTimeUtc());
        const double distKm  = msg.distKm;
        const double distDisp = distKm >= 0
            ? (m_distMiles ? distKm * k_kmToMi : distKm) : -1.0;

        switch (index.column()) {
            case ColAge:
                if (role == Qt::UserRole) return ageSecs;
                return ageSecs < 60
                    ? QStringLiteral("%1s").arg(ageSecs)
                    : QStringLiteral("%1m%2s").arg(ageSecs / 60).arg(ageSecs % 60, 2, 10, QChar('0'));
            case ColTime:
                return msg.utc.toString(QStringLiteral("HH:mm:ss"));
            case ColFreq:
                if (role == Qt::UserRole) return msg.audioFreqHz;
                return msg.audioFreqHz >= 0
                    ? QStringLiteral("+%1").arg(static_cast<int>(msg.audioFreqHz))
                    : QString::number(static_cast<int>(msg.audioFreqHz));
            case ColSnr:
                if (role == Qt::UserRole) return msg.snrDb;
                return QStringLiteral("%1").arg(msg.snrDb);
            case ColSubmode:
                return msg.submodeStr;
            case ColFrom:
                return msg.from.isEmpty() ? QStringLiteral("?") : msg.from;
            case ColGrid:
                return msg.grid;
            case ColDist:
                if (role == Qt::UserRole) return distDisp >= 0 ? distDisp : 1e9;
                if (distDisp < 0) return QString();
                return QStringLiteral("%1 %2")
                    .arg(static_cast<int>(distDisp))
                    .arg(m_distMiles ? QStringLiteral("mi") : QStringLiteral("km"));
            case ColBearing:
                if (role == Qt::UserRole) return msg.bearingDeg >= 0 ? msg.bearingDeg : 1e9;
                return msg.bearingDeg >= 0
                    ? QStringLiteral("%1°").arg(static_cast<int>(msg.bearingDeg))
                    : QString();
        }
    }

    if (role == Qt::ForegroundRole) {
        // CRC failure overrides all type colours
        if (msg.hasChecksum && !msg.checksumValid)
            return QBrush(QColor(0xff, 0x60, 0x60));        // red — bad CRC

        // Grid/dist/bearing from cache → dim
        if ((index.column() == ColGrid ||
             index.column() == ColDist ||
             index.column() == ColBearing) && msg.gridFromCache)
            return QBrush(QColor(0x55, 0x55, 0x66));        // dim grey

        switch (msg.type) {
            case JS8Message::Type::Heartbeat:
                return QBrush(QColor(0x7f, 0xbf, 0x7f));   // green
            case JS8Message::Type::SnrQuery:
            case JS8Message::Type::InfoQuery:
            case JS8Message::Type::StatusQuery:
            case JS8Message::Type::GridQuery:
            case JS8Message::Type::HearingQuery:
            case JS8Message::Type::QueryMsgs:
            case JS8Message::Type::QueryMsg:
                return QBrush(QColor(0xc9, 0xa8, 0x4c));   // amber
            case JS8Message::Type::DirectedMessage:
            case JS8Message::Type::MsgCommand:
                return QBrush(QColor(0xe8, 0xe0, 0xd0));   // bright white
            case JS8Message::Type::AckMessage:
                return QBrush(QColor(0x7f, 0xbf, 0xff));   // blue
            case JS8Message::Type::MsgAvailable:
            case JS8Message::Type::MsgDelivery:
                return QBrush(QColor(0xff, 0xd0, 0x80));   // orange/gold
            default:
                return QBrush(QColor(0x88, 0x88, 0x99));   // dim
        }
    }

    if (role == Qt::BackgroundRole) {
        if (msg.heardMe)
            return QBrush(QColor(0x1a, 0x3a, 0x1a));  // dark green tint — station replied to us
    }

    return {};
}

QVariant MessageModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
        case ColAge:     return QStringLiteral("Age");
        case ColTime:    return QStringLiteral("Time");
        case ColFreq:    return QStringLiteral("Freq");
        case ColSnr:     return QStringLiteral("SNR");
        case ColSubmode: return QStringLiteral("Mode");
        case ColFrom:    return QStringLiteral("From");
        case ColGrid:    return QStringLiteral("Grid");
        case ColDist:    return QStringLiteral("Dist");
        case ColBearing: return QStringLiteral("Bearing");
    }
    return {};
}

void MessageModel::sort(int column, Qt::SortOrder order)
{
    if (m_messages.isEmpty()) return;
    m_sortColumn = column;
    m_sortOrder  = order;

    beginResetModel();
    std::stable_sort(m_messages.begin(), m_messages.end(),
        [&](const JS8Message &a, const JS8Message &b) -> bool {
            bool less = false;
            switch (column) {
                case ColAge:  less = a.utc > b.utc; break;  // newer = smaller age
                case ColFreq: less = a.audioFreqHz < b.audioFreqHz; break;
                case ColSnr:  less = a.snrDb < b.snrDb; break;
                case ColDist: {
                    const double da = a.distKm >= 0 ? a.distKm * k_kmToMi : 1e9;
                    const double db = b.distKm >= 0 ? b.distKm * k_kmToMi : 1e9;
                    less = da < db; break;
                }
                default: less = false; break;
            }
            return order == Qt::AscendingOrder ? less : !less;
        });
    endResetModel();
    // Row 0 (@ALL) always stays at top — it's virtual, not in m_messages.
}

void MessageModel::setMaxAgeMins(int mins)
{
    m_maxAgeMins = qMax(1, mins);
}

void MessageModel::addMessage(const JS8Message &msg)
{
    // Don't show anonymous or hash-compressed callsigns (e.g. "<...>").
    if (msg.from.isEmpty() || msg.from.startsWith(QLatin1Char('<'))) return;

    // Upsert by callsign: if we've already heard this station, update its row.
    if (!msg.from.isEmpty()) {
        for (int i = 0; i < m_messages.size(); ++i) {
            if (m_messages[i].from.toUpper() == msg.from.toUpper()) {
                JS8Message &existing = m_messages[i];
                existing.utc        = msg.utc;
                existing.audioFreqHz = msg.audioFreqHz;
                existing.snrDb      = msg.snrDb;
                existing.submodeStr = msg.submodeStr;
                existing.submodeEnum = msg.submodeEnum;
                existing.type       = msg.type;
                if (msg.heardMe) existing.heardMe = true;  // sticky: once set, never cleared
                if (!msg.grid.isEmpty()) {
                    existing.grid         = msg.grid;
                    existing.gridFromCache = msg.gridFromCache;
                    existing.distKm       = msg.distKm;
                    existing.bearingDeg   = msg.bearingDeg;
                }
                emit dataChanged(index(i, 0), index(i, ColCount - 1));
                sort(m_sortColumn, m_sortOrder);
                return;
            }
        }
    }

    // New callsign — insert at row 0
    beginInsertRows({}, 0, 0);
    m_messages.prepend(msg);
    endInsertRows();

    if (m_messages.size() > k_maxRows) {
        beginRemoveRows({}, k_maxRows, m_messages.size() - 1);
        m_messages.resize(k_maxRows);
        endRemoveRows();
    }

    sort(m_sortColumn, m_sortOrder);
}

const JS8Message &MessageModel::messageAt(int msgIdx) const
{
    return m_messages.at(msgIdx);
}

void MessageModel::clear()
{
    if (m_messages.isEmpty()) return;
    beginResetModel();
    m_messages.clear();
    endResetModel();
}

void MessageModel::setDistanceMiles(bool miles)
{
    if (m_distMiles == miles) return;
    m_distMiles = miles;
    if (!m_messages.isEmpty())
        emit dataChanged(index(0, ColDist), index(m_messages.size() - 1, ColDist));
    emit headerDataChanged(Qt::Horizontal, ColDist, ColDist);
}
