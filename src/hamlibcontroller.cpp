// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from ~/ota/src/hamlibcontroller.cpp
#include "hamlibcontroller.h"
#include <QDebug>
#include <algorithm>
#include <cmath>

HamlibController::HamlibController(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<RigConfig>("RigConfig");
#ifdef HAVE_HAMLIB
    rig_load_all_backends();
    rig_set_debug(RIG_DEBUG_NONE);
#endif
}

HamlibController::~HamlibController()
{
    disconnectRig();
}

bool HamlibController::isConnected() const
{
    return m_connected.load();
}

bool HamlibController::connectRig(const RigConfig &cfg)
{
#ifdef HAVE_HAMLIB
    disconnectRig();

    m_rig = rig_init(cfg.rigModel);
    if (!m_rig) {
        emit error(QStringLiteral("Failed to initialise rig model %1").arg(cfg.rigModel));
        return false;
    }

    const QByteArray portBytes = cfg.port.toLocal8Bit();
    strncpy(m_rig->state.rigport.pathname, portBytes.constData(),
            HAMLIB_FILPATHLEN - 1);
    m_rig->state.rigport.timeout = k_portTimeoutMs;

    auto &ser = m_rig->state.rigport.parm.serial;
    ser.rate      = cfg.baudRate;
    ser.data_bits = cfg.dataBits;
    ser.stop_bits = cfg.stopBits;
    switch (cfg.parity) {
        case 1:  ser.parity = RIG_PARITY_ODD;   break;
        case 2:  ser.parity = RIG_PARITY_EVEN;  break;
        default: ser.parity = RIG_PARITY_NONE;  break;
    }
    switch (cfg.handshake) {
        case 1:  ser.handshake = RIG_HANDSHAKE_XONXOFF;  break;
        case 2:  ser.handshake = RIG_HANDSHAKE_HARDWARE; break;
        default: ser.handshake = RIG_HANDSHAKE_NONE;     break;
    }
    switch (cfg.dtrState) {
        case 1:  ser.dtr_state = RIG_SIGNAL_ON;    break;
        case 2:  ser.dtr_state = RIG_SIGNAL_OFF;   break;
        default: ser.dtr_state = RIG_SIGNAL_UNSET; break;
    }
    switch (cfg.rtsState) {
        case 1:  ser.rts_state = RIG_SIGNAL_ON;    break;
        case 2:  ser.rts_state = RIG_SIGNAL_OFF;   break;
        default: ser.rts_state = RIG_SIGNAL_UNSET; break;
    }

    m_pttType = cfg.pttType;
    switch (cfg.pttType) {
        case 1:  m_rig->state.pttport.type.ptt = RIG_PTT_RIG;         break;
        case 2:  m_rig->state.pttport.type.ptt = RIG_PTT_SERIAL_DTR;  break;
        case 3:  m_rig->state.pttport.type.ptt = RIG_PTT_SERIAL_RTS;  break;
        default: m_rig->state.pttport.type.ptt = RIG_PTT_NONE;        break;
    }
    if (cfg.pttType == 2 || cfg.pttType == 3)
        strncpy(m_rig->state.pttport.pathname, portBytes.constData(),
                HAMLIB_FILPATHLEN - 1);

    int ret = rig_open(m_rig);
    if (ret != RIG_OK) {
        emit error(QStringLiteral("Failed to open rig: %1")
                   .arg(QString::fromLatin1(rigerror(ret))));
        rig_cleanup(m_rig);
        m_rig = nullptr;
        return false;
    }

    m_consecutiveErrors = 0;
    m_connected.store(true);
    emit connectionChanged(true);
    return true;
#else
    Q_UNUSED(cfg)
    emit error(QStringLiteral("Hamlib not available in this build"));
    return false;
#endif
}

void HamlibController::disconnectRig()
{
#ifdef HAVE_HAMLIB
    if (m_rig) {
        rig_close(m_rig);
        rig_cleanup(m_rig);
        m_rig = nullptr;
    }
#endif
    if (m_connected.load()) {
        m_connected.store(false);
        emit connectionChanged(false);
    }
}

bool HamlibController::setFrequency(double khz)
{
#ifdef HAVE_HAMLIB
    if (!m_rig || !m_connected.load()) {
        emit error(QStringLiteral("Radio not connected"));
        return false;
    }
    freq_t freqHz = static_cast<freq_t>(khz * 1.0e3);
    int ret = rig_set_freq(m_rig, RIG_VFO_CURR, freqHz);
    if (ret != RIG_OK) {
        emit error(QStringLiteral("Failed to set frequency: %1")
                   .arg(QString::fromLatin1(rigerror(ret))));
        return false;
    }
    emit frequencyChanged(khz);
    return true;
#else
    Q_UNUSED(khz)
    emit error(QStringLiteral("Hamlib not available"));
    return false;
#endif
}

bool HamlibController::tune(double khz, const QString &mode)
{
#ifdef HAVE_HAMLIB
    if (!m_rig || !m_connected.load()) {
        emit error(QStringLiteral("Radio not connected"));
        return false;
    }
    const freq_t freqHz = static_cast<freq_t>(khz * 1.0e3);
    int ret = rig_set_freq(m_rig, RIG_VFO_CURR, freqHz);
    if (ret != RIG_OK) {
        emit error(QStringLiteral("Failed to set frequency: %1")
                   .arg(QString::fromLatin1(rigerror(ret))));
        return false;
    }
    emit frequencyChanged(khz);

    const QString modeUp = mode.toUpper();
    rmode_t rmode = RIG_MODE_NONE;
    if      (modeUp == QLatin1String("USB"))                         rmode = RIG_MODE_USB;
    else if (modeUp == QLatin1String("LSB"))                         rmode = RIG_MODE_LSB;
    else if (modeUp == QLatin1String("SSB"))
        rmode = (khz < 10000.0) ? RIG_MODE_LSB : RIG_MODE_USB;
    else if (modeUp == QLatin1String("DATA-U") ||
             modeUp == QLatin1String("PKT-U"))                       rmode = RIG_MODE_PKTUSB;
    if (rmode != RIG_MODE_NONE)
        rig_set_mode(m_rig, RIG_VFO_CURR, rmode, RIG_PASSBAND_NOCHANGE);
    return true;
#else
    Q_UNUSED(khz) Q_UNUSED(mode)
    emit error(QStringLiteral("Hamlib not available"));
    return false;
#endif
}

bool HamlibController::setPtt(bool transmit)
{
#ifdef HAVE_HAMLIB
    if (m_pttType == 0)
        return true;   // VOX — radio handles it
    if (!m_rig || !m_connected.load()) {
        emit error(QStringLiteral("Radio not connected"));
        return false;
    }
    const ptt_t ptt = transmit ? RIG_PTT_ON : RIG_PTT_OFF;
    int ret = rig_set_ptt(m_rig, RIG_VFO_CURR, ptt);
    if (ret != RIG_OK) {
        emit error(QStringLiteral("PTT %1 failed: %2")
                   .arg(transmit ? u"ON" : u"OFF")
                   .arg(QString::fromLatin1(rigerror(ret))));
        return false;
    }
    return true;
#else
    Q_UNUSED(transmit)
    emit error(QStringLiteral("Hamlib not available"));
    return false;
#endif
}

void HamlibController::startTune()
{
#ifdef HAVE_HAMLIB
    if (!m_rig || !m_connected.load()) return;
    rig_vfo_op(m_rig, RIG_VFO_CURR, RIG_OP_TUNE);
#endif
}

void HamlibController::requestPoll()
{
#ifdef HAVE_HAMLIB
    if (!m_rig || !m_connected.load()) return;
    const double khz = getFrequency();
    if (khz <= 0) {
        if (++m_consecutiveErrors >= k_maxConsecutiveErrors) {
            emit error(QStringLiteral("Radio connection lost"));
            disconnectRig();
        }
        return;
    }
    m_consecutiveErrors = 0;
    emit pollResult(khz, getMode());
#endif
}

double HamlibController::getFrequency() const
{
#ifdef HAVE_HAMLIB
    if (!m_rig || !m_connected.load()) return 0.0;
    freq_t freqHz = 0;
    if (rig_get_freq(m_rig, RIG_VFO_CURR, &freqHz) != RIG_OK) return 0.0;
    return static_cast<double>(freqHz) / 1.0e3;
#else
    return 0.0;
#endif
}

int HamlibController::getRfPower() const
{
#ifdef HAVE_HAMLIB
    if (!m_rig || !m_connected.load()) return -1;
    value_t val{};
    if (rig_get_level(m_rig, RIG_VFO_CURR, RIG_LEVEL_RFPOWER, &val) != RIG_OK) return -1;
    return qBound(0, static_cast<int>(std::round(val.f * 100.0f)), 100);
#else
    return -1;
#endif
}

int HamlibController::getAfVolume() const
{
#ifdef HAVE_HAMLIB
    if (!m_rig || !m_connected.load()) return -1;
    value_t val{};
    if (rig_get_level(m_rig, RIG_VFO_CURR, RIG_LEVEL_AF, &val) != RIG_OK) return -1;
    return qBound(0, static_cast<int>(std::round(val.f * 100.0f)), 100);
#else
    return -1;
#endif
}

int HamlibController::getMute() const
{
#ifdef HAVE_HAMLIB
    if (!m_rig || !m_connected.load()) return -1;
    int status = 0;
    int ret = rig_get_func(m_rig, RIG_VFO_CURR, RIG_FUNC_MUTE, &status);
    if (ret == RIG_OK) return status ? 1 : 0;
    // Fallback: infer mute state from whether we have a saved pre-mute volume
    return (m_preMuteVolume >= 0) ? 1 : 0;
#else
    return -1;
#endif
}

bool HamlibController::setRfPower(int pct)
{
#ifdef HAVE_HAMLIB
    if (!m_rig || !m_connected.load()) {
        m_lastError = QStringLiteral("Radio not connected");
        emit error(m_lastError);
        return false;
    }
    value_t val{};
    val.f = static_cast<float>(qBound(0, pct, 100)) / 100.0f;
    int ret = rig_set_level(m_rig, RIG_VFO_CURR, RIG_LEVEL_RFPOWER, val);
    if (ret != RIG_OK) {
        m_lastError = QStringLiteral("Failed to set RF power: %1")
                      .arg(QString::fromLatin1(rigerror(ret)));
        emit error(m_lastError);
        return false;
    }
    return true;
#else
    Q_UNUSED(pct)
    m_lastError = QStringLiteral("Hamlib not available");
    emit error(m_lastError);
    return false;
#endif
}

bool HamlibController::setAfVolume(int pct)
{
#ifdef HAVE_HAMLIB
    if (!m_rig || !m_connected.load()) {
        m_lastError = QStringLiteral("Radio not connected");
        emit error(m_lastError);
        return false;
    }
    value_t val{};
    val.f = static_cast<float>(qBound(0, pct, 100)) / 100.0f;
    int ret = rig_set_level(m_rig, RIG_VFO_CURR, RIG_LEVEL_AF, val);
    if (ret != RIG_OK) {
        m_lastError = QStringLiteral("Failed to set AF volume: %1")
                      .arg(QString::fromLatin1(rigerror(ret)));
        emit error(m_lastError);
        return false;
    }
    return true;
#else
    Q_UNUSED(pct)
    m_lastError = QStringLiteral("Hamlib not available");
    emit error(m_lastError);
    return false;
#endif
}

bool HamlibController::setMute(bool muted)
{
#ifdef HAVE_HAMLIB
    if (!m_rig || !m_connected.load()) {
        m_lastError = QStringLiteral("Radio not connected");
        emit error(m_lastError);
        return false;
    }
    int ret = rig_set_func(m_rig, RIG_VFO_CURR, RIG_FUNC_MUTE, muted ? 1 : 0);
    if (ret == RIG_OK) {
        if (!muted) m_preMuteVolume = -1;
        return true;
    }
    // RIG_FUNC_MUTE not supported — fall back to AF volume save/restore
    if (muted) {
        value_t vol{};
        if (rig_get_level(m_rig, RIG_VFO_CURR, RIG_LEVEL_AF, &vol) != RIG_OK) {
            m_lastError = QStringLiteral("Mute not supported and AF volume unavailable");
            emit error(m_lastError);
            return false;
        }
        m_preMuteVolume = qBound(0, static_cast<int>(std::round(vol.f * 100.0f)), 100);
        vol.f = 0.0f;
        if (rig_set_level(m_rig, RIG_VFO_CURR, RIG_LEVEL_AF, vol) != RIG_OK) {
            m_lastError = QStringLiteral("Mute not supported and failed to set AF volume to 0");
            emit error(m_lastError);
            return false;
        }
    } else {
        int restoreVol = (m_preMuteVolume >= 0) ? m_preMuteVolume : 20;
        m_preMuteVolume = -1;
        value_t vol{};
        vol.f = static_cast<float>(restoreVol) / 100.0f;
        if (rig_set_level(m_rig, RIG_VFO_CURR, RIG_LEVEL_AF, vol) != RIG_OK) {
            m_lastError = QStringLiteral("Unmute not supported and failed to restore AF volume");
            emit error(m_lastError);
            return false;
        }
    }
    return true;
#else
    Q_UNUSED(muted)
    m_lastError = QStringLiteral("Hamlib not available");
    emit error(m_lastError);
    return false;
#endif
}

QString HamlibController::getMode() const
{
#ifdef HAVE_HAMLIB
    if (!m_rig || !m_connected.load()) return QString();
    rmode_t rmode = RIG_MODE_NONE;
    pbwidth_t width = 0;
    if (rig_get_mode(m_rig, RIG_VFO_CURR, &rmode, &width) != RIG_OK) return QString();
    switch (rmode) {
        case RIG_MODE_USB:    return QStringLiteral("USB");
        case RIG_MODE_LSB:    return QStringLiteral("LSB");
        case RIG_MODE_CW:     return QStringLiteral("CW");
        case RIG_MODE_AM:     return QStringLiteral("AM");
        case RIG_MODE_FM:     return QStringLiteral("FM");
        case RIG_MODE_PKTUSB: return QStringLiteral("DATA-U");
        case RIG_MODE_PKTLSB: return QStringLiteral("DATA-L");
        default:              return QStringLiteral("?");
    }
#else
    return QString();
#endif
}

QStringList HamlibController::availableRigs() const
{
#ifdef HAVE_HAMLIB
    QStringList rigs;
    rig_list_foreach([](const struct rig_caps *caps, void *data) -> int {
        auto *list = static_cast<QStringList *>(data);
        list->append(QStringLiteral("%1 — %2 %3")
                     .arg(caps->rig_model)
                     .arg(QString::fromLatin1(caps->mfg_name))
                     .arg(QString::fromLatin1(caps->model_name)));
        return 1;
    }, &rigs);
    std::sort(rigs.begin(), rigs.end(), [](const QString &a, const QString &b) {
        auto nameOf = [](const QString &s) {
            int sep = s.indexOf(QLatin1String(" \u2014 "));
            return sep >= 0 ? s.mid(sep + 3) : s;
        };
        return nameOf(a).compare(nameOf(b), Qt::CaseInsensitive) < 0;
    });
    return rigs;
#else
    return {};
#endif
}
