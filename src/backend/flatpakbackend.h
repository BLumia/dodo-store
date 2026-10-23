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
class StoreApiClient;

// GUI-thread facade. Owns the worker thread, the exposed list models and the
// AppStream metadata pool. QML talks to this object only; all flatpak calls
// happen on the worker thread.
class FlatpakBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(AppListModel *installedModel READ installedModel CONSTANT)
    Q_PROPERTY(AppListModel *browseModel READ browseModel CONSTANT)
    // Home's curated lists. API-driven when the store API is enabled, or
    // locally derived (popular/updated) when it is disabled.
    Q_PROPERTY(AppListModel *trendingModel READ trendingModel CONSTANT)
    Q_PROPERTY(AppListModel *popularModel READ popularModel CONSTANT)
    Q_PROPERTY(AppListModel *newModel READ newModel CONSTANT)
    Q_PROPERTY(AppListModel *updatedModel READ updatedModel CONSTANT)
    Q_PROPERTY(bool storeApiEnabled READ storeApiEnabled NOTIFY storeApiEnabledChanged)
    // "", "loading", "ok", "stale" (cached data shown after a failed refresh)
    // or "error" (nothing could be loaded).
    Q_PROPERTY(QString trendingStatus READ trendingStatus NOTIFY homeStatusChanged)
    Q_PROPERTY(QString popularStatus READ popularStatus NOTIFY homeStatusChanged)
    Q_PROPERTY(QString newStatus READ newStatus NOTIFY homeStatusChanged)
    Q_PROPERTY(QString updatedStatus READ updatedStatus NOTIFY homeStatusChanged)
    // Browse page listing: a filtered, incrementally populated view over
    // browseModel (one category, or all applications). It references the items
    // owned by browseModel and never deletes them.
    Q_PROPERTY(AppListModel *browseListModel READ browseListModel CONSTANT)
    // Updates for both applications and runtimes; applications are listed first
    // so the Updates page can group them with ListView sections.
    Q_PROPERTY(AppListModel *updatesModel READ updatesModel CONSTANT)
    Q_PROPERTY(int applicationUpdateCount READ applicationUpdateCount NOTIFY updatesLoaded)

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
    AppListModel *browseListModel() const;
    AppListModel *trendingModel() const;
    AppListModel *popularModel() const;
    AppListModel *newModel() const;
    AppListModel *updatedModel() const;
    bool storeApiEnabled() const;
    QString trendingStatus() const;
    QString popularStatus() const;
    QString newStatus() const;
    QString updatedStatus() const;
    AppListModel *updatesModel() const;
    int applicationUpdateCount() const;

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

    // Browse page listing. showBrowseCategory() filters browseModel by an
    // AppStream category id (empty = all applications) and fills browseListModel
    // with the first chunk; loadMoreBrowse() appends the next chunk.
    Q_INVOKABLE void showBrowseCategory(const QString &category);
    Q_INVOKABLE void loadMoreBrowse();
    // Search within the current listing: the selected category, or all
    // applications when no category is active.
    Q_INVOKABLE void searchBrowse(const QString &term);
    // Leave the category listing (back to the category tiles).
    Q_INVOKABLE void clearBrowseCategory();

    // Home. The store API is optional: when disabled (or when no network is
    // used) Home shows a local popular list and the most recently released
    // applications from AppStream.
    Q_INVOKABLE void configureStoreApi(bool enabled, const QString &baseUrl,
                                       const QString &proxy);
    Q_INVOKABLE void refreshHome();

    // Operations. Accepts appId, remote, branch, arch; resolves into the worker.
    Q_INVOKABLE void installApp(const QString &appId, const QString &remote,
                                const QString &branch, const QString &arch);
    Q_INVOKABLE void uninstallApp(const QString &appId, const QString &branch,
                                  const QString &arch);
    // Starts an installed app through libflatpak (asynchronously; the result is
    // reported by launchFinished()).
    Q_INVOKABLE void launchApp(const QString &appId, const QString &branch,
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
    // Result of launchApp(): the app has been started, or why it could not be.
    void launchFinished(bool success, const QString &name, const QString &error);
    // action is "add", "remove" or "modify".
    void remoteOperationFinished(bool success, const QString &action,
                                 const QString &name, const QString &error);
    // Emitted once the app referenced by a "flatpak+https" link is present in
    // the browse model, so QML can navigate to its detail page.
    void openAppRequested(const QString &appId, const QString &branch);
    void flatpakRefFailed(const QString &url, const QString &error);
    void storeApiEnabledChanged();
    void homeStatusChanged();
    void homeLoaded();
    // A refresh failed but cached data is being shown (toast-worthy).
    void homeRefreshFailed(const QString &error);

private Q_SLOTS:
    void onInstalledRefsReady(QList<AppItem *> items);
    void onRemoteRefsReady(const QString &remote, QList<AppItem *> items);
    void onUpdatesRefsReady(QList<AppItem *> items);
    void onRemotesReady(const QVariantList &remotes);
    void onOperationFinished(const QString &ref, bool success, const QString &error);
    void onLaunchFinished(bool success, const QString &name, const QString &error);
    void onRemoteOperationFinished(bool success, const QString &action,
                                   const QString &name, const QString &error);
    void onFlatpakRefResolved(const QString &remote, const QString &appId,
                              const QString &branch);
    void onFlatpakRefFailed(const QString &url, const QString &error);
    void onApiCollectionReady(const QString &collection, const QVariantList &apps);
    void onApiCollectionFailed(const QString &collection, const QString &error,
                               bool hadCache, const QVariantList &cachedApps);
    void onApiRefreshStarted();
    void onProgressChanged(double value);
    void onStatusChanged(const QString &status);
    void onAppStreamLoaded(bool success);

private:
    void setBusy(bool busy);
    void fillInstalledModel(QList<AppItem *> items);
    void fillUpdatesModel();
    void enrichBatch(AppListModel *model, int from);

    // Home helpers.
    void loadOfflineHome();
    void fillHomeModel(AppListModel *model, const QVariantList &apps);
    AppListModel *homeModelFor(const QString &collection) const;
    void setHomeStatus(const QString &collection, const QString &status);
    static QString hostFlatpakArch();

    QThread *m_thread = nullptr;
    FlatpakWorker *m_worker = nullptr;
    AppStreamPoolBridge *m_appstream = nullptr;

    AppListModel *m_installedModel = nullptr;
    AppListModel *m_browseModel = nullptr;
    AppListModel *m_browseListModel = nullptr;
    AppListModel *m_trendingModel = nullptr;
    AppListModel *m_popularModel = nullptr;
    AppListModel *m_newModel = nullptr;
    AppListModel *m_updatedModel = nullptr;
    AppListModel *m_updatesModel = nullptr;

    StoreApiClient *m_storeApi = nullptr;
    bool m_storeApiEnabled = true;
    bool m_storeApiConfigured = false;
    bool m_refreshFailureEmitted = false;
    QString m_apiBaseUrl;
    QString m_apiProxy;
    QString m_trendingStatus;
    QString m_popularStatus;
    QString m_newStatus;
    QString m_updatedStatus;

    // Items of the active category (unfiltered) and the currently displayed
    // listing (category, or search results within it).
    QList<AppItem *> m_browseCategoryItems;
    QList<AppItem *> m_browseFiltered;
    int m_browseShown = 0;
    QString m_browseCategory;
    QString m_browseSearchTerm;
    bool m_browseCategoryActive = false;

    QStringList m_remotes;
    QVariantList m_remotesInfo;
    QString m_currentRemote;
    QString m_loadedBrowseRemote;
    bool m_busy = false;
    double m_progress = 0.0;
    QString m_status;

    QList<AppItem *> m_pendingInstalled;

    // Target of a pending "flatpak+https" link, awaited until the browse model
    // for its remote has been populated.
    QString m_pendingOpenAppId;
    QString m_pendingOpenBranch;
};