#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QDebug>
#include "MediaController.hpp"

int main(int argc, char *argv[]) {
    // Automotive IVI Display configuration
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QQuickStyle::setStyle("Material");

    QGuiApplication app(argc, argv);
    app.setApplicationName("OJT Automotive QML Video Player");
    app.setOrganizationName("OJT-Automotive");

    MediaController controller;

    // Command-line options: allow passing local media folder for host testing
    for (int i = 1; i < argc; ++i) {
        QString arg = argv[i];
        if (arg == "--dir" && i + 1 < argc) {
            controller.scanLocalDirectory(argv[++i]);
        }
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("mediaController", &controller);

    const QUrl url(QStringLiteral("qrc:/qml/main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl) {
            qCritical() << "Failed to load root QML component:" << url;
            QCoreApplication::exit(-1);
        }
    }, Qt::QueuedConnection);

    engine.load(url);

    qInfo() << "==========================================================";
    qInfo() << "  OJT Automotive Qt5/QML Video Player Initialized        ";
    qInfo() << "  Target / Host Mode: Active                             ";
    qInfo() << "==========================================================";

    return app.exec();
}
