// SPDX-FileCopyrightText: 2026 dodo-store authors
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "flatpakbackend.h"
#include "flatpakworker.h"
#include "appstreampoolbridge.h"
#include "../models/appitem.h"
#include "../models/applistmodel.h"

#include <QThread>
#include <QCoreApplication>
#include <QDebug>
#include <QMetaType>
#include <QUrl>

Q_DECLARE_METATYPE(QList<AppItem *>)

FlatpakBackend::FlatpakBackend(QObject *parent)
    : QObject(parent)
    , m_worker(new FlatpakWorker)
    , m_appstream(new AppStreamPoolBridge(this))
    , m_installedModel(new AppListModel(this))
    , m_browseModel(new AppListModel(this))
    , m_updatesModel(new AppListModel(this))
{
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
AppListModel *FlatpakBackend::updatesModel() const { return m_updatesModel; }

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
    setBusy(true);
    QMetaObject::invokeMethod(m_worker, "loadRemote", Qt::QueuedConnection,
                              Q_ARG(QString, m_currentRemote));
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
    const AppListModel *models[] = { m_browseModel, m_installedModel, m_updatesModel };
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
    // Populate the remote's app list; onRemoteRefsReady() then emits
    // openAppRequested() once the referenced app is actually present.
    refreshBrowse();
}

void FlatpakBackend::onFlatpakRefFailed(const QString &url, const QString &error)
{
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
    m_updatesModel->clear();
    m_updatesModel->appendItems(items);
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
        const QString next = m_remotes.isEmpty() ? QString() : m_remotes.first();
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
    if (success)
        QMetaObject::invokeMethod(m_worker, "loadRemotes", Qt::QueuedConnection);
    Q_EMIT remoteOperationFinished(success, action, name, error);
}

void FlatpakBackend::onRemoteRefsReady(const QString &remote, QList<AppItem *> items)
{
    Q_UNUSED(remote)
    m_browseModel->clear();
    m_browseModel->appendItems(items);
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
}

void FlatpakBackend::onOperationFinished(const QString &ref, bool success, const QString &error)
{
    setBusy(false);
    refreshInstalled();
    Q_EMIT operationFinished(ref, success, error);
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