#pragma once

#include <QObject>
#include <QString>

namespace bike::client {

class MapBridge : public QObject {
    Q_OBJECT
public:
    explicit MapBridge(QObject* parent = nullptr);

signals:
    void bikeClicked(const QString& bike_no);
    void bikeCountsUpdated(int idle, int damaged);

public slots:
    void onBikeClicked(const QString& bike_no) { emit bikeClicked(bike_no); }
    void onBikeCountsUpdated(int idle, int damaged) { emit bikeCountsUpdated(idle, damaged); }
};

} // namespace bike::client
