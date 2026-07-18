#include <QApplication>

#include "mainwindow.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("shared_bike_client");
    QApplication::setOrganizationName("wangya002");

    bike::client::MainWindow w;
    w.show();
    return app.exec();
}
