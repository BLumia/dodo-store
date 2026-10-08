// SPDX-FileCopyrightText: 2026 dodo-store authors
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QMutex>
#include <memory>

class AppItem;
class FlatpakWorkerPrivate;

// Runs blocking libflatpak calls off the GUI thread. The system libflatpak only
// exposes synchronous (blocking) APIs, so every flatpak operation is executed
// from this worker object (moved to a QThread). Results are marshalled back to
// the GUI thread with queued signals; model mutation happens on the GUI thread.
//
// AppStream metadata (names, summaries, descriptions, icons, screenshots) is
// read on the GUI thread by FlatpakBackend via AppStreamQt's async Pool.
class FlatpakWorker : public QObject
{
    Q_OBJECT
public:
    explicit FlatpakWorker(QObject *parent = nullptr);
    ~FlatpakWorker() override;

    // Listing (blocking, run on worker thread).
    Q_INVOKABLE void loadInstalled();
    Q_INVOKABLE void loadRemote(const QString &remote);
    Q_INVOKABLE void loadRemotes();
    Q_INVOKABLE void loadUpdates();

    // Remote management (blocking, run on worker thread). @a url may point at a
    // plain repository directory or at a ".flatpakrepo" description file, which
    // is fetched and parsed so its URL, GPG key and metadata are applied.
    Q_INVOKABLE void addRemote(const QString &name, const QString &url, bool gpgVerify);
    Q_INVOKABLE void modifyRemote(const QString &name, const QString &title, const QString &url,
                                  bool gpgVerify, bool noEnumerate, int priority);
    Q_INVOKABLE void removeRemote(const QString &name);
    Q_INVOKABLE void setRemoteEnabled(const QString &name, bool enabled);

    // Operations. The ref fields come from an AppItem:
    //   ref = "<kind>/<name>/<arch>/<branch>"
    Q_INVOKABLE void install(const QString &remote, const QString &kind, const QString &name,
                             const QString &arch, const QString &branch);
    Q_INVOKABLE void uninstall(const QString &kind, const QString &name,
                               const QString &arch, const QString &branch);
    Q_INVOKABLE void updateApp(const QString &kind, const QString &name,
                               const QString &arch, const QString &branch);
    Q_INVOKABLE void updateAll();

    // Flatpak web-link support: fetch and parse a ".flatpakref" URL (as opened
    // through the "flatpak+https" scheme) to the remote, app id and branch it
    // points at. The referenced remote is added when not configured yet, so the
    // app page can be shown for the ref.
    Q_INVOKABLE void resolveFlatpakRef(const QString &url);

    // Request cancellation of the current blocking operation (thread-safe).
    void requestCancel();

    // Thread-safe progress state read from the GUI thread.
    double progressValue() const;
    QString progressStatus() const;

Q_SIGNALS:
    void installedRefsReady(QList<AppItem *> items);
    void remoteRefsReady(const QString &remote, QList<AppItem *> items);
    void updatesRefsReady(QList<AppItem *> items);
    // One map per remote: name, title, url, comment, description, homepage,
    // disabled, gpgVerify, noEnumerate, prio.
    void remotesReady(const QVariantList &remotes);
    void remoteOperationFinished(bool success, const QString &action,
                                 const QString &name, const QString &error);
    void operationFinished(const QString &ref, bool success, const QString &error);
    // Result of resolveFlatpakRef(): the configured remote, app id and branch a
    // ".flatpakref" link points at, or the reason it could not be resolved.
    void flatpakRefResolved(const QString &remote, const QString &appId,
                            const QString &branch);
    void flatpakRefFailed(const QString &url, const QString &error);
    void progressChanged(double value);
    void statusChanged(const QString &status);

private:
    std::unique_ptr<FlatpakWorkerPrivate> d;
};