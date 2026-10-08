// SPDX-FileCopyrightText: 2026 dodo-store authors
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include "../models/applistmodel.h"

class AppItem;
class FlatpakWorker;
class QThread;
class AppStreamPoolBridge;

// GUI-thread facade. Owns the worker thread, the exposed list models and the
// AppStream metadata pool. QML talks to this object only; all flatpak calls
// happen on the worker thread.
class FlatpakBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(AppListModel *installedModel READ installedModel CONSTANT)
    Q_PROPERTY(AppListModel *browseModel READ browseModel CONSTANT)
    Q_PROPERTY(AppListModel *updatesModel READ updatesModel CONSTANT)
    Q_PROPERTY(QStringList remotes READ remotes NOTIFY remotesChanged)
    // Detailed view of the configured remotes (one map per remote) for the
    // remote management page: name, title, url, comment, description, homepage,
    // disabled, gpgVerify, noEnumerate, prio.
    Q_PROPERTY(QVariantList remotesInfo READ remotesInfo NOTIFY remotesInfoChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString currentRemote READ currentRemote WRITE setCurrentRemote NOTIFY currentRemoteChanged)

public:
    explicit FlatpakBackend(QObject *parent = nullptr);
    ~FlatpakBackend() override;

    AppListModel *installedModel() const;
    AppListModel *browseModel() const;
    AppListModel *updatesModel() const;
    QStringList remotes() const;
    QVariantList remotesInfo() const;
    bool busy() const;
    double progress() const;
    QString status() const;
    QString currentRemote() const;
    void setCurrentRemote(const QString &remote);

    // Refresh the installed / updates models (async).
    Q_INVOKABLE void refreshInstalled();
    // Populate the browse model for the current remote (async).
    Q_INVOKABLE void refreshBrowse();
    Q_INVOKABLE void search(const QString &term);

    // Operations. Accepts appId, remote, branch, arch; resolves into the worker.
    Q_INVOKABLE void installApp(const QString &appId, const QString &remote,
                                const QString &branch, const QString &arch);
    Q_INVOKABLE void uninstallApp(const QString &appId, const QString &branch,
                                  const QString &arch);
    Q_INVOKABLE void updateApp(const QString &appId, const QString &branch,
                               const QString &arch);
    Q_INVOKABLE void updateAll();
    Q_INVOKABLE void cancelOperation();

    // Remote management. addRemote accepts a plain repository URL or a
    // ".flatpakrepo" description URL; gpgVerify only applies to the former.
    Q_INVOKABLE void addRemote(const QString &name, const QString &url, bool gpgVerify);
    Q_INVOKABLE void editRemote(const QString &name, const QString &title, const QString &url,
                                bool gpgVerify, bool noEnumerate, int priority);
    Q_INVOKABLE void removeRemote(const QString &name);
    Q_INVOKABLE void setRemoteEnabled(const QString &name, bool enabled);

    // Enrich an item's display metadata from AppStream (called after loading).
    Q_INVOKABLE void refreshMetadata();

    // Flatpak web-link support. Accepts a "flatpak+https://..." URL (as opened
    // by the x-scheme-handler/flatpak+https desktop entry), a plain http(s)
    // URL, or a local path/URI to a ".flatpakref". The ref is resolved on the
    // worker thread; when it names an app in the browse model,
    // openAppRequested() is emitted so QML can show its detail page.
    Q_INVOKABLE void openFlatpakRefUrl(const QString &url);

    // Lookup by app id and branch in the browse model, used to navigate after
    // resolving a web link. Returns nullptr when the app is not listed.
    Q_INVOKABLE QObject *findAppItem(const QString &appId, const QString &branch) const;

    // Lookup by AppItem::key() across the browse/installed/updates models.
    // The detail page re-resolves its item through this after a model reload,
    // because a reload deletes the AppItems it was opened with.
    Q_INVOKABLE QObject *findAppByKey(const QString &key) const;

    // Lookup by AppItem::key() ("remote:appId:branch"). The detail page uses
    // these instead of holding on to an AppItem pointer, which the model may
    // delete when a refresh repopulates the lists.
    Q_INVOKABLE bool isInstalled(const QString &key) const;
    Q_INVOKABLE bool hasUpdate(const QString &key) const;

Q_SIGNALS:
    void remotesChanged();
    void remotesInfoChanged();
    void busyChanged();
    void progressChanged();
    void statusChanged();
    void currentRemoteChanged();
    void browseLoaded();
    void installedLoaded();
    void updatesLoaded();
    void operationFinished(const QString &ref, bool success, const QString &error);
    // action is "add", "remove" or "modify".
    void remoteOperationFinished(bool success, const QString &action,
                                 const QString &name, const QString &error);
    // Emitted once the app referenced by a "flatpak+https" link is present in
    // the browse model, so QML can navigate to its detail page.
    void openAppRequested(const QString &appId, const QString &branch);
    void flatpakRefFailed(const QString &url, const QString &error);

private Q_SLOTS:
    void onInstalledRefsReady(QList<AppItem *> items);
    void onRemoteRefsReady(const QString &remote, QList<AppItem *> items);
    void onUpdatesRefsReady(QList<AppItem *> items);
    void onRemotesReady(const QVariantList &remotes);
    void onOperationFinished(const QString &ref, bool success, const QString &error);
    void onRemoteOperationFinished(bool success, const QString &action,
                                   const QString &name, const QString &error);
    void onFlatpakRefResolved(const QString &remote, const QString &appId,
                              const QString &branch);
    void onFlatpakRefFailed(const QString &url, const QString &error);
    void onProgressChanged(double value);
    void onStatusChanged(const QString &status);
    void onAppStreamLoaded(bool success);

private:
    void setBusy(bool busy);
    void fillInstalledModel(QList<AppItem *> items);
    void fillUpdatesModel();
    void enrichBatch(AppListModel *model, int from);

    QThread *m_thread = nullptr;
    FlatpakWorker *m_worker = nullptr;
    AppStreamPoolBridge *m_appstream = nullptr;

    AppListModel *m_installedModel = nullptr;
    AppListModel *m_browseModel = nullptr;
    AppListModel *m_updatesModel = nullptr;

    QStringList m_remotes;
    QVariantList m_remotesInfo;
    QString m_currentRemote;
    bool m_busy = false;
    double m_progress = 0.0;
    QString m_status;

    QList<AppItem *> m_pendingInstalled;

    // Target of a pending "flatpak+https" link, awaited until the browse model
    // for its remote has been populated.
    QString m_pendingOpenAppId;
    QString m_pendingOpenBranch;
};