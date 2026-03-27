// SPDX-License-Identifier: GPL-3.0-or-later
#include "qsolog.h"
#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

static const QString kConnName = QStringLiteral("jf8call_qsolog");

QString QsoLog::dbPath()
{
    return QDir::homePath() + QStringLiteral("/.jf8call/qsolog.db");
}

QsoLog &QsoLog::instance()
{
    static QsoLog s;
    return s;
}

QsoLog::QsoLog()
{
    openDb();
}

bool QsoLog::openDb()
{
    QDir().mkpath(QDir::homePath() + QStringLiteral("/.jf8call"));

    QSqlDatabase db = QSqlDatabase::contains(kConnName)
        ? QSqlDatabase::database(kConnName)
        : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kConnName);
    db.setDatabaseName(dbPath());
    if (!db.open()) {
        qWarning() << "QsoLog: failed to open" << dbPath() << db.lastError().text();
        return false;
    }

    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS qsos ("
        "  id       INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  utc      TEXT NOT NULL,"
        "  callsign TEXT NOT NULL,"
        "  grid     TEXT,"
        "  band     TEXT,"
        "  mode     TEXT,"
        "  freq_khz REAL,"
        "  tx_freq  REAL,"
        "  snr      INTEGER,"
        "  sent_rst TEXT,"
        "  rcvd_rst TEXT,"
        "  notes    TEXT"
        ")"))) {
        qWarning() << "QsoLog: CREATE TABLE failed:" << q.lastError().text();
        return false;
    }
    m_open = true;
    return true;
}

int QsoLog::addQso(const QsoEntry &e)
{
    if (!m_open) return -1;
    QSqlDatabase db = QSqlDatabase::database(kConnName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO qsos (utc,callsign,grid,band,mode,freq_khz,tx_freq,snr,sent_rst,rcvd_rst,notes)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(e.utc.isValid() ? e.utc.toString(Qt::ISODate)
                                   : QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    q.addBindValue(e.callsign);
    q.addBindValue(e.grid);
    q.addBindValue(e.band);
    q.addBindValue(e.mode);
    q.addBindValue(e.freqKhz);
    q.addBindValue(e.txFreqHz);
    q.addBindValue(e.snrDb);
    q.addBindValue(e.sentRst);
    q.addBindValue(e.rcvdRst);
    q.addBindValue(e.notes);
    if (!q.exec()) {
        qWarning() << "QsoLog: INSERT failed:" << q.lastError().text();
        return -1;
    }
    return static_cast<int>(q.lastInsertId().toLongLong());
}

void QsoLog::removeQso(int id)
{
    if (!m_open) return;
    QSqlQuery q(QSqlDatabase::database(kConnName));
    q.prepare(QStringLiteral("DELETE FROM qsos WHERE id=?"));
    q.addBindValue(id);
    q.exec();
}

void QsoLog::clear()
{
    if (!m_open) return;
    QSqlQuery q(QSqlDatabase::database(kConnName));
    q.exec(QStringLiteral("DELETE FROM qsos"));
}

QList<QsoEntry> QsoLog::all() const
{
    QList<QsoEntry> out;
    if (!m_open) return out;
    QSqlQuery q(QSqlDatabase::database(kConnName));
    q.exec(QStringLiteral(
        "SELECT id,utc,callsign,grid,band,mode,freq_khz,tx_freq,snr,sent_rst,rcvd_rst,notes"
        " FROM qsos ORDER BY utc DESC"));
    while (q.next()) {
        QsoEntry e;
        e.id        = q.value(0).toInt();
        e.utc       = QDateTime::fromString(q.value(1).toString(), Qt::ISODate);
        e.callsign  = q.value(2).toString();
        e.grid      = q.value(3).toString();
        e.band      = q.value(4).toString();
        e.mode      = q.value(5).toString();
        e.freqKhz   = q.value(6).toDouble();
        e.txFreqHz  = q.value(7).toDouble();
        e.snrDb     = q.value(8).toInt();
        e.sentRst   = q.value(9).toString();
        e.rcvdRst   = q.value(10).toString();
        e.notes     = q.value(11).toString();
        out.append(e);
    }
    return out;
}

int QsoLog::count() const
{
    if (!m_open) return 0;
    QSqlQuery q(QSqlDatabase::database(kConnName));
    q.exec(QStringLiteral("SELECT COUNT(*) FROM qsos"));
    return q.next() ? q.value(0).toInt() : 0;
}

// ── ADIF export ──────────────────────────────────────────────────────────────

static QString adifField(const QString &name, const QString &val)
{
    if (val.isEmpty()) return QString();
    return QStringLiteral("<%1:%2>%3 ").arg(name).arg(val.size()).arg(val);
}

QString QsoLog::exportAdif() const
{
    QString out;
    out += QStringLiteral("ADIF Export from JF8Call\n");
    out += QStringLiteral("<ADIF_VER:5>3.1.0 <PROGRAMID:7>JF8Call <PROGRAMVERSION:5>0.5.2 <EOH>\n\n");

    for (const QsoEntry &e : all()) {
        // Date: YYYYMMDD, Time: HHMMSS
        const QString date = e.utc.toString(QStringLiteral("yyyyMMdd"));
        const QString time = e.utc.toString(QStringLiteral("HHmmss"));
        // Frequency in MHz
        const QString freqMhz = QString::number(e.freqKhz / 1000.0, 'f', 6);

        out += adifField(QStringLiteral("CALL"),      e.callsign);
        out += adifField(QStringLiteral("QSO_DATE"),  date);
        out += adifField(QStringLiteral("TIME_ON"),   time);
        out += adifField(QStringLiteral("FREQ"),      freqMhz);
        out += adifField(QStringLiteral("BAND"),      e.band);
        out += adifField(QStringLiteral("MODE"),      e.mode == QStringLiteral("JS8")
                                                      ? QStringLiteral("JS8")
                                                      : e.mode);
        out += adifField(QStringLiteral("GRIDSQUARE"),e.grid);
        out += adifField(QStringLiteral("RST_SENT"),  e.sentRst);
        out += adifField(QStringLiteral("RST_RCVD"),  e.rcvdRst);
        if (e.snrDb != 0) {
            out += adifField(QStringLiteral("RST_RCVD"),
                             QStringLiteral("%1 dB SNR").arg(e.snrDb));
        }
        out += adifField(QStringLiteral("COMMENT"),   e.notes);
        out += QStringLiteral("<EOR>\n");
    }
    return out;
}

// ── ADIF import ──────────────────────────────────────────────────────────────

// Parse all fields from one ADIF record into a map <FIELDNAME_UPPER → value>
static QHash<QString, QString> parseAdifRecord(const QString &rec)
{
    QHash<QString, QString> fields;
    int pos = 0;
    while (pos < rec.size()) {
        // Find '<'
        const int lt = rec.indexOf(QLatin1Char('<'), pos);
        if (lt < 0) break;
        const int gt = rec.indexOf(QLatin1Char('>'), lt);
        if (gt < 0) break;
        const QString tag = rec.mid(lt + 1, gt - lt - 1);
        // tag is: NAME or NAME:LEN or NAME:LEN:TYPE
        const QStringList parts = tag.split(QLatin1Char(':'));
        if (parts.isEmpty()) { pos = gt + 1; continue; }
        const QString name = parts[0].toUpper();
        if (name == QStringLiteral("EOR") || name == QStringLiteral("EOH")) {
            pos = gt + 1;
            break;
        }
        int len = (parts.size() > 1) ? parts[1].toInt() : 0;
        const QString value = (len > 0) ? rec.mid(gt + 1, len) : QString();
        if (!name.isEmpty()) fields[name] = value;
        pos = gt + 1 + std::max(0, len);
    }
    return fields;
}

// Determine amateur band from frequency in MHz
static QString bandFromMhz(double mhz)
{
    if (mhz >=   1.8  && mhz <=   2.0)  return QStringLiteral("160m");
    if (mhz >=   3.5  && mhz <=   4.0)  return QStringLiteral("80m");
    if (mhz >=   5.25 && mhz <=   5.45) return QStringLiteral("60m");
    if (mhz >=   7.0  && mhz <=   7.3)  return QStringLiteral("40m");
    if (mhz >=  10.1  && mhz <=  10.15) return QStringLiteral("30m");
    if (mhz >=  14.0  && mhz <=  14.35) return QStringLiteral("20m");
    if (mhz >=  18.068&& mhz <=  18.168)return QStringLiteral("17m");
    if (mhz >=  21.0  && mhz <=  21.45) return QStringLiteral("15m");
    if (mhz >=  24.89 && mhz <=  24.99) return QStringLiteral("12m");
    if (mhz >=  28.0  && mhz <=  29.7)  return QStringLiteral("10m");
    if (mhz >=  50.0  && mhz <=  54.0)  return QStringLiteral("6m");
    if (mhz >= 144.0  && mhz <= 148.0)  return QStringLiteral("2m");
    return QString();
}

int QsoLog::importAdif(const QString &adif, QString *errorMsg)
{
    if (!m_open) {
        if (errorMsg) *errorMsg = QStringLiteral("Database not open");
        return -1;
    }

    int added = 0;
    // Skip header (everything before <EOH>)
    int startPos = 0;
    const int eoh = adif.toUpper().indexOf(QStringLiteral("<EOH>"));
    if (eoh >= 0) startPos = eoh + 5;

    const QString body = adif.mid(startPos);

    // Split on EOR markers
    int recStart = 0;
    while (recStart < body.size()) {
        const int eorPos = body.toUpper().indexOf(QStringLiteral("<EOR>"), recStart);
        if (eorPos < 0) break;
        const QString record = body.mid(recStart, eorPos - recStart);
        recStart = eorPos + 5;

        const auto fields = parseAdifRecord(record);
        if (fields.isEmpty()) continue;

        QsoEntry e;
        e.callsign = fields.value(QStringLiteral("CALL")).toUpper();
        if (e.callsign.isEmpty()) continue;

        // Date + time
        const QString date = fields.value(QStringLiteral("QSO_DATE"));
        const QString time = fields.value(QStringLiteral("TIME_ON"), QStringLiteral("000000"));
        if (date.size() == 8) {
            e.utc = QDateTime::fromString(date + time.left(6).leftJustified(6, QLatin1Char('0')),
                                          QStringLiteral("yyyyMMddHHmmss"));
            e.utc.setTimeZone(QTimeZone::utc());
        }

        e.grid    = fields.value(QStringLiteral("GRIDSQUARE")).toUpper();
        e.mode    = fields.value(QStringLiteral("MODE"));
        e.sentRst = fields.value(QStringLiteral("RST_SENT"), QStringLiteral("599"));
        e.rcvdRst = fields.value(QStringLiteral("RST_RCVD"), QStringLiteral("599"));
        e.notes   = fields.value(QStringLiteral("COMMENT"));
        if (e.notes.isEmpty())
            e.notes = fields.value(QStringLiteral("NOTES"));

        // Frequency
        const double mhz = fields.value(QStringLiteral("FREQ")).toDouble();
        e.freqKhz = mhz * 1000.0;

        e.band = fields.value(QStringLiteral("BAND"));
        if (e.band.isEmpty() && mhz > 0)
            e.band = bandFromMhz(mhz);

        if (addQso(e) > 0) ++added;
    }
    return added;
}
