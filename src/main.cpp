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
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QStringList>

#include <DLog>

#include "backend/flatpakbackend.h"
#include "models/appfilterproxymodel.h"

namespace {

// The flatpak web link (or ".flatpakref" path) passed on the command line, if
// any. Flathub launches the scheme handler as
// "dodo-store flatpak+https://.../org.example.App.flatpakref".
QString webLinkFromArguments(const QStringList &args)
{
    for (int i = 1; i < args.size(); ++i) {
        const QString arg = args.at(i);
        if (arg.startsWith(QLatin1String("flatpak+"), Qt::CaseInsensitive))
            return arg;
        if (arg.endsWith(QLatin1String(".flatpakref"), Qt::CaseInsensitive)
            && (arg.startsWith(QLatin1String("http"), Qt::CaseInsensitive)
                || arg.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)
                || arg.startsWith(QLatin1Char('/'))))
            return arg;
    }
    return QString();
}

// Per-user socket used to forward a web link to a running instance.
QString singleInstanceSocketPath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty())
        dir = QDir::tempPath();
    return dir + QStringLiteral("/dodo-store.sock");
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("deepin");
    app.setApplicationName("dodo-store");
    app.setApplicationVersion("0.1.0");

    Dtk::Core::DLogManager::registerConsoleAppender();

    // A web link may target an already-running store: forward it and exit, so
    // the existing window navigates instead of a second window opening.
    const QString webLink = webLinkFromArguments(app.arguments());
    const QString socketPath = singleInstanceSocketPath();

    QLocalSocket client;
    client.connectToServer(socketPath);
    if (client.waitForConnected(300)) {
        if (!webLink.isEmpty()) {
            client.write(webLink.toUtf8());
            client.flush();
            client.waitForBytesWritten(500);
        }
        return 0;
    }

    QLocalServer server;
    QLocalServer::removeServer(socketPath);
    server.listen(socketPath);

    QQmlApplicationEngine engine;

    // Deepin's Chameleon Qt Quick Controls style ships with dtkdeclarative.
    QQuickStyle::setStyle("Chameleon");

    // C++ filter proxy for the large browse/installed catalogs.
    qmlRegisterType<AppFilterProxyModel>("dodo.store", 1, 0, "AppFilterProxyModel");

    // The single backend instance; QML talks to this object exclusively.
    FlatpakBackend backend;
    engine.rootContext()->setContextProperty("flatpakBackend", &backend);

    QObject::connect(&server, &QLocalServer::newConnection, &app, [&server, &backend] {
        while (QLocalSocket *socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket, &backend] {
                const QString url = QString::fromUtf8(socket->readAll());
                if (!url.isEmpty())
                    backend.openFlatpakRefUrl(url);
            });
            QObject::connect(socket, &QLocalSocket::disconnected, socket,
                             &QObject::deleteLater);
        }
    });

    engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    // Kick off the initial listing on the worker thread. When the remotes are
    // known, load the default remote's app list.
    QObject::connect(&backend, &FlatpakBackend::remotesChanged, &backend, [&backend] {
        backend.refreshBrowse();
    });
    backend.refreshInstalled();

    // Handle a link delivered with this (first) launch.
    if (!webLink.isEmpty())
        backend.openFlatpakRefUrl(webLink);

    return app.exec();
}
