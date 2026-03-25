#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from ~/ota/src/hamlibcontroller.h

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMetaType>
#include <atomic>

#ifdef HAVE_HAMLIB
#include <hamlib/rig.h>
#endif

struct RigConfig {
    int     rigModel   = 1;
    QString port;
    int     baudRate   = 9600;
    int     dataBits   = 8;
    int     stopBits   = 1;
    int     parity     = 0;      // 0=None 1=Odd 2=Even
    int     handshake  = 0;      // 0=None 1=XON/XOFF 2=Hardware
    int     dtrState   = 0;      // 0=unset 1=on 2=off
    int     rtsState   = 0;
    int     pttType    = 0;      // 0=VOX/none 1=CAT 2=DTR 3=RTS
};
Q_DECLARE_METATYPE(RigConfig)

class HamlibController : public QObject {
    Q_OBJECT
public:
    explicit HamlibController(QObject *parent = nullptr);
    ~HamlibController();

    bool isConnected() const;
    QStringList availableRigs() const;

    Q_INVOKABLE double  getFrequency() const;
    Q_INVOKABLE QString getMode()      const;

public slots:
    bool connectRig(const RigConfig &cfg);
    void disconnectRig();
    bool setFrequency(double khz);
    bool tune(double khz, const QString &mode = QString());
    bool setPtt(bool transmit);
    void requestPoll();
    void startTune();   // trigger ATU tuning cycle (RIG_OP_TUNE)

signals:
    void connectionChanged(bool connected);
    void frequencyChanged(double khz);
    void pollResult(double khz, QString mode);
    void error(QString message);

private:
#ifdef HAVE_HAMLIB
    RIG *m_rig = nullptr;
#endif
    std::atomic<bool> m_connected{false};
    int  m_pttType           = 0;
    int  m_consecutiveErrors = 0;
    static constexpr int k_maxConsecutiveErrors = 5;
    static constexpr int k_portTimeoutMs        = 2000;
};
