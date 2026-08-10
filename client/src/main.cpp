#include "location_provider.hpp"
#include "map_bridge.hpp"
#include "net/api_bridge.hpp"
#include "trajectory_model.hpp"

#include <QGuiApplication>
#include <QProcessEnvironment>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#if !defined(Q_OS_ANDROID)
// Qt WebEngine 不支持 Android (任务 #55): Android 上 QML 侧走 QtWebView 降级路径,
// 不初始化 WebEngineQuick。
#include <QtWebEngineQuick/QtWebEngineQuick>
#endif

int main(int argc, char** argv) {
#if !defined(Q_OS_ANDROID)
    QtWebEngineQuick::initialize();
#endif
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("shared_bike_client");
    QGuiApplication::setOrganizationName("wangya002");

    // 服务器地址: 环境变量 BIKE_SERVER_HOST / BIKE_SERVER_PORT 覆盖,
    // 默认现网云服务器 124.220.92.243:8888 (与旧 MainWindow 一致)。
    const auto env = QProcessEnvironment::systemEnvironment();
    const QString host = env.value(QStringLiteral("BIKE_SERVER_HOST"),
                                   QStringLiteral("124.220.92.243"));
    const quint16 port = static_cast<quint16>(
        env.value(QStringLiteral("BIKE_SERVER_PORT"), QStringLiteral("8888")).toUInt());

    bike::client::ApiBridge api(host, port);
    bike::client::MapBridge mapBridge;
    bike::client::TrajectoryModel trajectory;
    bike::client::LocationProvider location;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("api"), &api);
    engine.rootContext()->setContextProperty(QStringLiteral("mapBridge"), &mapBridge);
    engine.rootContext()->setContextProperty(QStringLiteral("trajectory"), &trajectory);
    engine.rootContext()->setContextProperty(QStringLiteral("location"), &location);
    engine.loadFromModule("Bike", "Main");
    if (engine.rootObjects().isEmpty()) return -1;

    return app.exec();
}
