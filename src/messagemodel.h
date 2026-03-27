#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QAbstractTableModel>
#include <QList>
#include <QTimer>
#include "jf8message.h"

class MessageModel : public QAbstractTableModel {
    Q_OBJECT
public:
    // ColTo removed; row 0 is always the virtual @ALL entry.
    enum Column { ColAge=0, ColTime, ColFreq, ColSnr, ColSubmode, ColFrom,
                  ColGrid, ColDist, ColBearing, ColCount };

    explicit MessageModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    void addMessage(const JF8Message &msg);

    // 0-based index into m_messages (not model row — model row = msgIdx + 1).
    const JF8Message &messageAt(int msgIdx) const;
    int  messageCount() const { return m_messages.size(); }

    void clear();
    void setDistanceMiles(bool miles);
    void setMaxAgeMins(int mins);

private:
    static constexpr int k_maxRows = 1000;
    static constexpr double k_kmToMi = 0.621371;
    bool m_distMiles  = true;
    int  m_maxAgeMins = 30;
    QList<JF8Message> m_messages;  // newest at index 0 (model row 1)
    QTimer *m_ageTimer = nullptr;
    int  m_sortColumn = ColAge;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
};
