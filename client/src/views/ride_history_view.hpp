#pragma once

#include "backend_client.hpp"
#include "session_model.hpp"

#include <QPushButton>
#include <QTableWidget>
#include <QWidget>
#include <QLabel>

namespace bike::client {

class RideHistoryView : public QWidget {
    Q_OBJECT
public:
    RideHistoryView(BackendClient* client, SessionModel* session, QWidget* parent = nullptr);

    void refresh();

signals:
    void viewDetailRequested(const QString& ride_no);

private slots:
    void on_row_double_clicked(int row);

private:
    BackendClient* client_;
    SessionModel*  session_;
    QTableWidget*  table_{nullptr};
    QPushButton*   btn_refresh_{nullptr};
    QLabel*        lb_status_{nullptr};
};

} // namespace bike::client
