#pragma once

#include <QWidget>

#include "backend_client.hpp"
#include "session_model.hpp"

class QTableWidget;
class QPushButton;

namespace bike::client {

class RecordsView : public QWidget {
    Q_OBJECT
public:
    RecordsView(BackendClient* client, SessionModel* session, QWidget* parent = nullptr);

public slots:
    void refresh();

private:
    BackendClient* client_;
    SessionModel* session_;
    QTableWidget* table_{nullptr};
    QPushButton* btn_refresh_{nullptr};
};

} // namespace bike::client
