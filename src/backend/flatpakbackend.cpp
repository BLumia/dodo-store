// SPDX-FileCopyrightText: 2026 dodo-store authors
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "flatpakbackend.h"
#include "flatpakworker.h"
#include "appstreampoolbridge.h"
#include "storeapiclient.h"
#include "../models/appitem.h"
#include "../models/applistmodel.h"

#include <QThread>
#include <QCoreApplication>
#include <QDebug>
#include <QMetaType>
#include <QSet>
#include <QSysInfo>
#include <QUrl>
#include <QVariantMap>

Q_DECLARE_METATYPE(QList<AppItem *>)

FlatpakBackend::FlatpakBackend(QObject *parent)
    : QObject(parent)
    , m_worker(new FlatpakWorker)
    , m_appstream(new AppStreamPoolBridge(this))
    , m_installedModel(new AppListModel(this))
    , m_browseModel(new AppListModel(this))
    , m_browseListModel(new AppListModel(this))
    , m_trendingModel(new AppListModel(this))
    , m_popularModel(new AppListModel(this))
    , m_newModel(new AppListModel(this))
    , m_updatedModel(new AppListModel(this))
    , m_updatesModel(new AppListModel(this))
    , m_storeApi(new StoreApiClient(this))
{
    // browseListModel is a filtered view of browseModel; browseModel owns the
    // AppItems, so clearing the view must not delete them.
    m_browseListModel->setOwnsItems(false);

    connect(m_storeApi, &StoreApiClient::collectionReady,
            this, &FlatpakBackend::onApiCollectionReady);
    connect(m_storeApi, &StoreApiClient::collectionFailed,
            this, &FlatpakBackend::onApiCollectionFailed);
    connect(m_storeApi, &StoreApiClient::refreshStarted,
            this, &FlatpakBackend::onApiRefreshStarted);

    qRegisterMetaType<QList<AppItem *>>("QList<AppItem*>");

    m_thread = new QThread(this);
    m_worker->moveToThread(m_thread);

    connect(m_worker, &FlatpakWorker::installedRefsReady, this, &FlatpakBackend::onInstalledRefsReady);
    connect(m_worker, &FlatpakWorker::remoteRefsReady, this, &FlatpakBackend::onRemoteRefsReady);
    connect(m_worker, &FlatpakWorker::updatesRefsReady, this, &FlatpakBackend::onUpdatesRefsReady);
    connect(m_worker, &FlatpakWorker::remotesReady, this, &FlatpakBackend::onRemotesReady);
    connect(m_worker, &FlatpakWorker::remoteOperationFinished,
            this, &FlatpakBackend::onRemoteOperationFinished);
    connect(m_worker, &FlatpakWorker::operationFinished, this, &FlatpakBackend::onOperationFinished);
    connect(m_worker, &FlatpakWorker::launchFinished, this, &FlatpakBackend::onLaunchFinished);
    connect(m_worker, &FlatpakWorker::flatpakRefResolved,
            this, &FlatpakBackend::onFlatpakRefResolved);
    connect(m_worker, &FlatpakWorker::flatpakRefFailed,
            this, &FlatpakBackend::onFlatpakRefFailed);
    connect(m_worker, &FlatpakWorker::progressChanged, this, &FlatpakBackend::onProgressChanged);
    connect(m_worker, &FlatpakWorker::statusChanged, this, &FlatpakBackend::onStatusChanged);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread->start();

    connect(m_appstream, &AppStreamPoolBridge::loadFinished, this, &FlatpakBackend::onAppStreamLoaded);
    m_appstream->startLoading();
}

FlatpakBackend::~FlatpakBackend()
{
    m_thread->quit();
    m_thread->wait();
}

AppListModel *FlatpakBackend::installedModel() const { return m_installedModel; }
AppListModel *FlatpakBackend::browseModel() const { return m_browseModel; }
AppListModel *FlatpakBackend::browseListModel() const { return m_browseListModel; }
AppListModel *FlatpakBackend::trendingModel() const { return m_trendingModel; }
AppListModel *FlatpakBackend::popularModel() const { return m_popularModel; }
AppListModel *FlatpakBackend::newModel() const { return m_newModel; }
AppListModel *FlatpakBackend::updatedModel() const { return m_updatedModel; }
bool FlatpakBackend::storeApiEnabled() const { return m_storeApiEnabled; }
QString FlatpakBackend::trendingStatus() const { return m_trendingStatus; }
QString FlatpakBackend::popularStatus() const { return m_popularStatus; }
QString FlatpakBackend::newStatus() const { return m_newStatus; }
QString FlatpakBackend::updatedStatus() const { return m_updatedStatus; }
AppListModel *FlatpakBackend::updatesModel() const { return m_updatesModel; }

int FlatpakBackend::applicationUpdateCount() const
{
    int count = 0;
    for (AppItem *item : m_updatesModel->items()) {
        if (item && !item->isRuntime())
            ++count;
    }
    return count;
}

QStringList FlatpakBackend::remotes() const { return m_remotes; }
QVariantList FlatpakBackend::remotesInfo() const { return m_remotesInfo; }
bool FlatpakBackend::busy() const { return m_busy; }
double FlatpakBackend::progress() const { return m_progress; }
QString FlatpakBackend::status() const { return m_status; }
QString FlatpakBackend::currentRemote() const { return m_currentRemote; }

void FlatpakBackend::setCurrentRemote(const QString &remote)
{
    if (m_currentRemote == remote)
        return;
    m_currentRemote = remote;
    Q_EMIT currentRemoteChanged();
}

void FlatpakBackend::setBusy(bool busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        Q_EMIT busyChanged();
    }
}

void FlatpakBackend::refreshInstalled()
{
    setBusy(true);
    QMetaObject::invokeMethod(m_worker, "loadInstalled", Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_worker, "loadUpdates", Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_worker, "loadRemotes", Qt::QueuedConnection);
}

void FlatpakBackend::refreshBrowse()
{
    if (m_currentRemote.isEmpty())
        return;


    // Reuse the loaded catalog when returning to the same remote.
    // A pending flatpakref request bypasses the cache so its app can be resolved.
    if (m_pendingOpenAppId.isEmpty()
        && m_loadedBrowseRemote == m_currentRemote
        && m_browseModel->count() > 0)
        return;

    loadBrowseRemote();
}

void FlatpakBackend::loadBrowseRemote()
{
    if (m_currentRemote.isEmpty())
        return;

    setBusy(true);
    QMetaObject::invokeMethod(m_worker, "loadRemote", Qt::QueuedConnection,
                              Q_ARG(QString, m_currentRemote));
}

namespace {
// How many rows the Browse listing gains per loadMoreBrowse() call.
constexpr int kBrowseChunkSize = 100;
} // namespace

void FlatpakBackend::showBrowseCategory(const QString &category)
{
    m_browseCategory = category;
    m_browseCategoryActive = true;
    m_browseSearchTerm.clear();

    // browseModel holds every app of the current remote; keep the matching
    // ones for this category (empty category = all applications).
    const QList<AppItem *> all = m_browseModel->items();
    m_browseCategoryItems.clear();
    if (category.isEmpty()) {
        m_browseCategoryItems = all;
    } else {
        const QStringList ids = m_appstream->appIdsInCategory(category);
        if (!ids.isEmpty()) {
            const QSet<QString> wanted(ids.begin(), ids.end());
            m_browseCategoryItems.reserve(all.size());
            for (AppItem *item : all) {
                if (item && wanted.contains(item->appId()))
                    m_browseCategoryItems.append(item);
            }
        }
    }

    m_browseFiltered = m_browseCategoryItems;
    m_browseShown = 0;
    m_browseListModel->clear();
    loadMoreBrowse();
}

void FlatpakBackend::searchBrowse(const QString &term)
{
    m_browseSearchTerm = term;
    if (term.isEmpty()) {
        if (m_browseCategoryActive)
            showBrowseCategory(m_browseCategory);
        else
            clearBrowseCategory();
        return;
    }

    // Search only within the current scope: the selected category, or the
    // whole catalog when the category tiles are shown.
    const QList<AppItem *> base =
        m_browseCategoryActive ? m_browseCategoryItems : m_browseModel->items();
    QList<AppItem *> matches;
    matches.reserve(base.size());
    for (AppItem *item : base) {
        if (!item)
            continue;
        if (item->name().contains(term, Qt::CaseInsensitive)
            || item->summary().contains(term, Qt::CaseInsensitive)
            || item->appId().contains(term, Qt::CaseInsensitive)) {
            matches.append(item);
        }
    }

    m_browseFiltered = matches;
    m_browseShown = 0;
    m_browseListModel->clear();
    loadMoreBrowse();
}

void FlatpakBackend::loadMoreBrowse()
{
    const int total = m_browseFiltered.size();
    if (m_browseShown >= total)
        return;

    const int end = qMin(m_browseShown + kBrowseChunkSize, total);
    m_browseListModel->appendItems(
        m_browseFiltered.mid(m_browseShown, end - m_browseShown));
    m_browseShown = end;
    qDebug() << "browse:" << m_browseCategory << m_browseSearchTerm
             << "- showing" << m_browseShown << "of" << total;
}

void FlatpakBackend::clearBrowseCategory()
{
    m_browseCategoryActive = false;
    m_browseCategory.clear();
    m_browseSearchTerm.clear();
    m_browseCategoryItems.clear();
    m_browseFiltered.clear();
    m_browseShown = 0;
    m_browseListModel->clear();
}

void FlatpakBackend::configureStoreApi(bool enabled, const QString &baseUrl,
                                       const QString &proxy)
{
    const bool changed = !m_storeApiConfigured
        || enabled != m_storeApiEnabled
        || baseUrl != m_apiBaseUrl
        || proxy != m_apiProxy;

    m_storeApiConfigured = true;
    qDebug() << "store api: configure enabled=" << enabled << "base=" << baseUrl << "proxy=" << proxy;
    m_storeApiEnabled = enabled;
    m_apiBaseUrl = baseUrl;
    m_apiProxy = proxy;
    m_storeApi->configure(baseUrl, proxy);

    if (!changed)
        return;

    m_trendingModel->clear();
    m_popularModel->clear();
    m_newModel->clear();
    m_updatedModel->clear();
    m_trendingStatus.clear();
    m_popularStatus.clear();
    m_newStatus.clear();
    m_updatedStatus.clear();
    m_refreshFailureEmitted = false;

    Q_EMIT storeApiEnabledChanged();
    Q_EMIT homeStatusChanged();

    if (m_storeApiEnabled)
        m_storeApi->refresh(false);
    else
        loadOfflineHome();
}

void FlatpakBackend::refreshHome()
{
    if (m_storeApiEnabled)
        m_storeApi->refresh(true);
    else
        loadOfflineHome();
}

void FlatpakBackend::onApiRefreshStarted()
{
    if (!m_storeApiEnabled)
        return;

    m_trendingStatus = QStringLiteral("loading");
    m_popularStatus = QStringLiteral("loading");
    m_newStatus = QStringLiteral("loading");
    m_updatedStatus = QStringLiteral("loading");
    m_refreshFailureEmitted = false;
    Q_EMIT homeStatusChanged();
}

void FlatpakBackend::onApiCollectionReady(const QString &collection,
                                          const QVariantList &apps)
{
    if (!m_storeApiEnabled)
        return;

    AppListModel *model = homeModelFor(collection);
    if (!model)
        return;

    fillHomeModel(model, apps);
    setHomeStatus(collection, QStringLiteral("ok"));
    Q_EMIT homeLoaded();
}

void FlatpakBackend::onApiCollectionFailed(const QString &collection,
                                           const QString &error, bool hadCache,
                                           const QVariantList &cachedApps)
{
    if (!m_storeApiEnabled)
        return;

    AppListModel *model = homeModelFor(collection);
    if (!model)
        return;

    if (hadCache) {
        fillHomeModel(model, cachedApps);
        setHomeStatus(collection, QStringLiteral("stale"));
        if (!m_refreshFailureEmitted) {
            m_refreshFailureEmitted = true;
            Q_EMIT homeRefreshFailed(error);
        }
    } else {
        model->clear();
        setHomeStatus(collection, QStringLiteral("error"));
    }
    Q_EMIT homeLoaded();
}

void FlatpakBackend::loadOfflineHome()
{
    // Trending/New need Flathub's server-side statistics; hide them offline.
    m_trendingModel->clear();
    m_newModel->clear();
    m_trendingStatus.clear();
    m_newStatus.clear();

    if (!m_appstream->loaded()) {
        m_popularStatus = QStringLiteral("loading");
        m_updatedStatus = QStringLiteral("loading");
        Q_EMIT homeStatusChanged();
        return;
    }

    const QString arch = hostFlatpakArch();

    // A small curated list of well-known applications, only kept when the
    // local AppStream data knows them.
    static const QStringList popularIds = {
        QStringLiteral("org.mozilla.firefox"),
        QStringLiteral("org.videolan.VLC"),
        QStringLiteral("org.telegram.desktop"),
        QStringLiteral("com.spotify.Client"),
        QStringLiteral("com.discordapp.Discord"),
        QStringLiteral("org.gimp.GIMP"),
        QStringLiteral("org.kde.krita"),
        QStringLiteral("org.inkscape.Inkscape"),
        QStringLiteral("org.libreoffice.LibreOffice"),
        QStringLiteral("com.valvesoftware.Steam"),
        QStringLiteral("com.usebottles.bottles"),
        QStringLiteral("com.obsproject.Studio"),
        QStringLiteral("org.audacityteam.Audacity"),
        QStringLiteral("org.blender.Blender"),
        QStringLiteral("org.localsend.localsend_app"),
        QStringLiteral("com.rustdesk.RustDesk"),
        QStringLiteral("org.gnome.TextEditor"),
        QStringLiteral("org.gnome.Calculator"),
        QStringLiteral("com.github.tchx84.Flatseal"),
        QStringLiteral("md.obsidian.Obsidian"),
        QStringLiteral("org.gnome.Loupe"),
        QStringLiteral("org.gnome.Fractal"),
        QStringLiteral("io.github.celluloid_player.Celluloid"),
        QStringLiteral("org.gnome.Geary"),
    };

    QList<AppItem *> popular;
    popular.reserve(popularIds.size());
    for (const QString &appId : popularIds) {
        if (!m_appstream->hasComponent(appId))
            continue;
        auto *item = new AppItem(appId, QStringLiteral("flathub"),
                                 QStringLiteral("stable"), arch, false);
        m_appstream->enrich(item);
        popular.append(item);
    }
    m_popularModel->clear();
    m_popularModel->appendItems(popular);
    m_popularStatus = QStringLiteral("ok");

    // "Updated": newest AppStream release date first.
    const QStringList updatedIds = m_appstream->recentlyUpdatedAppIds(24);
    QList<AppItem *> updated;
    updated.reserve(updatedIds.size());
    for (const QString &appId : updatedIds) {
        auto *item = new AppItem(appId, QStringLiteral("flathub"),
                                 QStringLiteral("stable"), arch, false);
        m_appstream->enrich(item);
        updated.append(item);
    }
    m_updatedModel->clear();
    m_updatedModel->appendItems(updated);
    m_updatedStatus = QStringLiteral("ok");

    Q_EMIT homeStatusChanged();
    Q_EMIT homeLoaded();
}

void FlatpakBackend::fillHomeModel(AppListModel *model, const QVariantList &apps)
{
    if (!model)
        return;

    const QString arch = hostFlatpakArch();
    QList<AppItem *> items;
    items.reserve(apps.size());

    for (const QVariant &entry : apps) {
        const QVariantMap map = entry.toMap();
        const QString appId = map.value(QStringLiteral("app_id")).toString();
        if (appId.isEmpty())
            continue;

        // The API lists the architectures an app is published for.
        const QVariantList arches = map.value(QStringLiteral("arches")).toList();
        if (!arches.isEmpty()) {
            bool supported = false;
            for (const QVariant &value : arches) {
                if (value.toString() == arch) {
                    supported = true;
                    break;
                }
            }
            if (!supported)
                continue;
        }

        auto *item = new AppItem(appId, QStringLiteral("flathub"),
                                 QStringLiteral("stable"), arch, false);
        item->setName(map.value(QStringLiteral("name")).toString());
        item->setSummary(map.value(QStringLiteral("summary")).toString());
        item->setDescription(map.value(QStringLiteral("description")).toString());
        item->setDeveloper(map.value(QStringLiteral("developer_name")).toString());
        const QUrl icon(map.value(QStringLiteral("icon")).toString());
        if (!icon.isEmpty())
            item->setIcon(icon);

        // Prefer the localized AppStream metadata; the API text is a fallback.
        m_appstream->enrich(item);
        items.append(item);
    }

    model->clear();
    model->appendItems(items);
}

AppListModel *FlatpakBackend::homeModelFor(const QString &collection) const
{
    if (collection == QLatin1String("trending"))
        return m_trendingModel;
    if (collection == QLatin1String("popular"))
        return m_popularModel;
    if (collection == QLatin1String("recently-added"))
        return m_newModel;
    if (collection == QLatin1String("recently-updated"))
        return m_updatedModel;
    return nullptr;
}

void FlatpakBackend::setHomeStatus(const QString &collection, const QString &status)
{
    QString *target = nullptr;
    if (collection == QLatin1String("trending"))
        target = &m_trendingStatus;
    else if (collection == QLatin1String("popular"))
        target = &m_popularStatus;
    else if (collection == QLatin1String("recently-added"))
        target = &m_newStatus;
    else if (collection == QLatin1String("recently-updated"))
        target = &m_updatedStatus;

    if (!target || *target == status)
        return;
    *target = status;
    Q_EMIT homeStatusChanged();
}

QString FlatpakBackend::hostFlatpakArch()
{
    const QString arch = QSysInfo::buildCpuArchitecture();
    if (arch == QLatin1String("arm64"))
        return QStringLiteral("aarch64");
    return arch;
}

void FlatpakBackend::refreshMetadata()
{
    for (auto *item : m_installedModel->items())
        m_appstream->enrich(item);
    for (auto *item : m_browseModel->items())
        m_appstream->enrich(item);
    for (auto *item : m_updatesModel->items())
        m_appstream->enrich(item);
    Q_EMIT installedLoaded();
    Q_EMIT browseLoaded();
    Q_EMIT updatesLoaded();
}

void FlatpakBackend::search(const QString &term)
{
    Q_UNUSED(term)
    // View-layer filtering; TODO(P2).
}

bool FlatpakBackend::isInstalled(const QString &key) const
{
    return m_installedModel->indexOfKey(key) >= 0;
}

bool FlatpakBackend::hasUpdate(const QString &key) const
{
    return m_updatesModel->indexOfKey(key) >= 0;
}

void FlatpakBackend::installApp(const QString &appId, const QString &remote,
                                const QString &branch, const QString &arch)
{
    setBusy(true);
    QMetaObject::invokeMethod(m_worker, "install", Qt::QueuedConnection,
                              Q_ARG(QString, remote), Q_ARG(QString, QStringLiteral("app")),
                              Q_ARG(QString, appId), Q_ARG(QString, arch),
                              Q_ARG(QString, branch));
}

void FlatpakBackend::uninstallApp(const QString &appId, const QString &branch,
                                  const QString &arch)
{
    setBusy(true);
    QMetaObject::invokeMethod(m_worker, "uninstall", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("app")), Q_ARG(QString, appId),
                              Q_ARG(QString, arch), Q_ARG(QString, branch));
}

void FlatpakBackend::launchApp(const QString &appId, const QString &branch,
                               const QString &arch)
{
    if (appId.isEmpty())
        return;

    // Launching is quick and fire-and-forget, so it does not toggle `busy`;
    // failures arrive asynchronously through launchFinished().
    QMetaObject::invokeMethod(m_worker, "launch", Qt::QueuedConnection,
                              Q_ARG(QString, appId), Q_ARG(QString, arch),
                              Q_ARG(QString, branch));
}

void FlatpakBackend::updateApp(const QString &appId, const QString &branch,
                               const QString &arch)
{
    setBusy(true);
    QMetaObject::invokeMethod(m_worker, "updateApp", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("app")), Q_ARG(QString, appId),
                              Q_ARG(QString, arch), Q_ARG(QString, branch));
}

void FlatpakBackend::updateAll()
{
    setBusy(true);
    QMetaObject::invokeMethod(m_worker, "updateAll", Qt::QueuedConnection);
}

void FlatpakBackend::cancelOperation()
{
    m_worker->requestCancel();
}

void FlatpakBackend::addRemote(const QString &name, const QString &url, bool gpgVerify)
{
    setBusy(true);
    QMetaObject::invokeMethod(m_worker, "addRemote", Qt::QueuedConnection,
                              Q_ARG(QString, name), Q_ARG(QString, url),
                              Q_ARG(bool, gpgVerify));
}

void FlatpakBackend::editRemote(const QString &name, const QString &title, const QString &url,
                                bool gpgVerify, bool noEnumerate, int priority)
{
    setBusy(true);
    QMetaObject::invokeMethod(m_worker, "modifyRemote", Qt::QueuedConnection,
                              Q_ARG(QString, name), Q_ARG(QString, title),
                              Q_ARG(QString, url), Q_ARG(bool, gpgVerify),
                              Q_ARG(bool, noEnumerate), Q_ARG(int, priority));
}

void FlatpakBackend::removeRemote(const QString &name)
{
    setBusy(true);
    QMetaObject::invokeMethod(m_worker, "removeRemote", Qt::QueuedConnection,
                              Q_ARG(QString, name));
}

void FlatpakBackend::setRemoteEnabled(const QString &name, bool enabled)
{
    setBusy(true);
    QMetaObject::invokeMethod(m_worker, "setRemoteEnabled", Qt::QueuedConnection,
                              Q_ARG(QString, name), Q_ARG(bool, enabled));
}

void FlatpakBackend::openFlatpakRefUrl(const QString &url)
{
    QString target = url.trimmed();
    if (target.isEmpty())
        return;

    // "flatpak+https://..." / "flatpak+http://..." from the web-link handler.
    if (target.startsWith(QLatin1String("flatpak+"), Qt::CaseInsensitive))
        target = target.mid(8);
    // A downloaded ".flatpakref" arrives as a local path; make it a file URI.
    if (target.startsWith(QLatin1Char('/')))
        target = QUrl::fromLocalFile(target).toString();

    setBusy(true);
    QMetaObject::invokeMethod(m_worker, "resolveFlatpakRef", Qt::QueuedConnection,
                              Q_ARG(QString, target));
}

QObject *FlatpakBackend::findAppItem(const QString &appId, const QString &branch) const
{
    const QList<AppItem *> items = m_browseModel->items();
    for (AppItem *item : items) {
        if (item && item->appId() == appId && item->branch() == branch)
            return item;
    }
    return nullptr;
}

QObject *FlatpakBackend::findAppByKey(const QString &key) const
{
    const AppListModel *models[] = {
        m_browseModel, m_installedModel, m_updatesModel,
        m_trendingModel, m_popularModel, m_newModel, m_updatedModel,
    };
    for (const AppListModel *model : models) {
        const QList<AppItem *> items = model->items();
        for (AppItem *item : items) {
            if (item && item->key() == key)
                return item;
        }
    }
    return nullptr;
}

void FlatpakBackend::onFlatpakRefResolved(const QString &remote, const QString &appId,
                                          const QString &branch)
{
    setBusy(false);
    if (m_currentRemote != remote) {
        m_currentRemote = remote;
        Q_EMIT currentRemoteChanged();
    }

    // The catalog may already hold the app (e.g. the link arrived while the
    // store was open): navigate straight away, without reloading the remote.
    if (findAppItem(appId, branch)) {
        Q_EMIT openAppRequested(appId, branch);
        return;
    }

    m_pendingOpenAppId = appId;
    m_pendingOpenBranch = branch;
    // Force a lookup when the link target was not in the currently loaded list.
    loadBrowseRemote();
}

void FlatpakBackend::onFlatpakRefFailed(const QString &url, const QString &error)
{
    qWarning() << "flatpak: cannot open link" << url << ":" << error;
    setBusy(false);
    Q_EMIT flatpakRefFailed(url, error);
}

void FlatpakBackend::onInstalledRefsReady(QList<AppItem *> items)
{
    m_installedModel->clear();
    m_installedModel->appendItems(items);
    enrichBatch(m_installedModel, 0);
    setBusy(false);
    Q_EMIT installedLoaded();
}

void FlatpakBackend::onUpdatesRefsReady(QList<AppItem *> items)
{
    // Applications first, then runtimes. The Updates page groups the rows with
    // ListView sections, which require each group's rows to be contiguous.
    QList<AppItem *> ordered;
    ordered.reserve(items.size());
    for (AppItem *item : items) {
        if (item && !item->isRuntime())
            ordered.append(item);
    }
    for (AppItem *item : items) {
        if (item && item->isRuntime())
            ordered.append(item);
    }

    m_updatesModel->clear();
    m_updatesModel->appendItems(ordered);
    enrichBatch(m_updatesModel, 0);
    setBusy(false);
    Q_EMIT updatesLoaded();
}

void FlatpakBackend::onRemotesReady(const QVariantList &remotes)
{
    m_remotesInfo = remotes;

    QStringList names;
    names.reserve(remotes.size());
    for (const QVariant &entry : remotes) {
        const QString name = entry.toMap().value(QStringLiteral("name")).toString();
        if (!name.isEmpty())
            names.append(name);
    }
    m_remotes = names;

    // Keep the browsed remote valid: if it disappeared (e.g. was removed) fall
    // back to the first configured remote.
    if (!m_remotes.contains(m_currentRemote)) {
        const QString next = m_remotes.isEmpty() ? QString()
            : (m_remotes.contains(QStringLiteral("flathub"))
                   ? QStringLiteral("flathub") : m_remotes.first());
        if (m_currentRemote != next) {
            m_currentRemote = next;
            Q_EMIT currentRemoteChanged();
        }
    }

    Q_EMIT remotesChanged();
    Q_EMIT remotesInfoChanged();
}

void FlatpakBackend::onRemoteOperationFinished(bool success, const QString &action,
                                               const QString &name, const QString &error)
{
    setBusy(false);
    if (success) {

        // Changes to configured remotes can invalidate the loaded catalog.

        m_loadedBrowseRemote.clear();
        QMetaObject::invokeMethod(m_worker, "loadRemotes", Qt::QueuedConnection);
    }
    Q_EMIT remoteOperationFinished(success, action, name, error);
}

void FlatpakBackend::onRemoteRefsReady(const QString &remote, QList<AppItem *> items)
{
    // The Browse listing references items owned by browseModel; drop those
    // references before browseModel deletes them.
    m_browseListModel->clear();
    m_browseCategoryItems.clear();
    m_browseFiltered.clear();
    m_browseShown = 0;

    m_browseModel->clear();
    m_browseModel->appendItems(items);
    m_loadedBrowseRemote = remote;
    enrichBatch(m_browseModel, 0);
    setBusy(false);
    Q_EMIT browseLoaded();

    // A "flatpak+https" link is waiting for this remote's app list: now that
    // the catalog is populated, hand the referenced app over to the UI.
    if (!m_pendingOpenAppId.isEmpty()) {
        const QString appId = m_pendingOpenAppId;
        const QString branch = m_pendingOpenBranch;
        m_pendingOpenAppId.clear();
        m_pendingOpenBranch.clear();
        Q_EMIT openAppRequested(appId, branch);
    }

    // Re-apply the active category / all-applications listing (or the search
    // within it) to the new catalog.
    if (!m_browseSearchTerm.isEmpty())
        searchBrowse(m_browseSearchTerm);
    else if (m_browseCategoryActive)
        showBrowseCategory(m_browseCategory);
}

void FlatpakBackend::onOperationFinished(const QString &ref, bool success, const QString &error)
{
    setBusy(false);
    refreshInstalled();
    Q_EMIT operationFinished(ref, success, error);
}

void FlatpakBackend::onLaunchFinished(bool success, const QString &name,
                                      const QString &error)
{
    if (!success)
        qWarning() << "flatpak: failed to launch" << name << ":" << error;
    Q_EMIT launchFinished(success, name, error);
}

void FlatpakBackend::onProgressChanged(double value)
{
    m_progress = value;
    Q_EMIT progressChanged();
}

void FlatpakBackend::onStatusChanged(const QString &status)
{
    m_status = status;
    Q_EMIT statusChanged();
}

void FlatpakBackend::onAppStreamLoaded(bool success)
{
    if (!success) {
        qWarning() << "AppStream pool failed to load; showing raw ref data.";
        return;
    }
    enrichBatch(m_installedModel, 0);
    enrichBatch(m_browseModel, 0);
    enrichBatch(m_updatesModel, 0);

    // Home items may have been created before the AppStream pool was ready.
    for (AppListModel *model : {m_trendingModel, m_popularModel, m_newModel,
                                m_updatedModel}) {
        for (AppItem *item : model->items())
            m_appstream->enrich(item);
    }
    if (!m_storeApiEnabled)
        loadOfflineHome();

    // The AppStream category index is only available once the pool is loaded.
    // If a category (or a search within one) was opened before that, rebuild
    // its listing now.
    if (!m_browseSearchTerm.isEmpty())
        searchBrowse(m_browseSearchTerm);
    else if (m_browseCategoryActive)
        showBrowseCategory(m_browseCategory);

    Q_EMIT installedLoaded();
    Q_EMIT browseLoaded();
    Q_EMIT updatesLoaded();
}

// Enriches at most kEnrichBatchSize rows per event-loop pass, then re-queues
// itself. A 4000-row catalog would otherwise busy the GUI thread for seconds
// right when the model fills (page loads, "Check for updates", ...).
void FlatpakBackend::enrichBatch(AppListModel *model, int from)
{
    if (!m_appstream->loaded())
        return;
    static constexpr int kEnrichBatchSize = 200;
    const QList<AppItem *> items = model->items();
    const int end = qMin(from + kEnrichBatchSize, items.size());
    for (int i = from; i < end; ++i)
        m_appstream->enrich(items.at(i));
    if (end < items.size())
        QMetaObject::invokeMethod(this, [this, model, end] { enrichBatch(model, end); },
                                  Qt::QueuedConnection);
}