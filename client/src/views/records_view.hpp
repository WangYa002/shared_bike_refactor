#pragma once

#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QTableWidget>
#include <QWidget>

#include "backend_client.hpp"
#include "session_model.hpp"

namespace bike::client {

class RecordsView : public QWidget {
    Q_OBJECT
public:
    RecordsView(BackendClient* client, SessionModel* session, QWidget* parent = nullptr);

public slots:
    void refresh();

private:
    void set_status(const QString& text, bool is_err = false, bool is_ok = false);

    BackendClient* client_;
    SessionModel* session_;
    QTableWidget* table_{nullptr};
    QPushButton* btn_refresh_{nullptr};
    QLabel* lb_status_{nullptr};
};

} // namespace bike::client
