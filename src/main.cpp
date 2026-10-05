// SPDX-FileCopyrightText: 2026 dodo-store authors
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QUrl>
#include <QDebug>

#include <DLog>

#include "backend/flatpakbackend.h"
#include "models/appfilterproxymodel.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("deepin");
    app.setApplicationName("dodo-store");
    app.setApplicationVersion("0.1.0");

    Dtk::Core::DLogManager::registerConsoleAppender();

    QQmlApplicationEngine engine;

    // Deepin's Chameleon Qt Quick Controls style ships with dtkdeclarative.
    QQuickStyle::setStyle("Chameleon");

    // C++ filter proxy for the large browse/installed catalogs.
    qmlRegisterType<AppFilterProxyModel>("dodo.store", 1, 0, "AppFilterProxyModel");

    // The single backend instance; QML talks to this object exclusively.
    FlatpakBackend backend;
    engine.rootContext()->setContextProperty("flatpakBackend", &backend);

    engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    // Kick off the initial listing on the worker thread. When the remotes are
    // known, load the default remote's app list.
    QObject::connect(&backend, &FlatpakBackend::remotesChanged, &backend, [&backend] {
        backend.refreshBrowse();
    });
    backend.refreshInstalled();

    return app.exec();
}