// SPDX-FileCopyrightText: 2026 dodo-store authors
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "flatpakworker.h"
#include "../models/appitem.h"

// Qt defines a "signals" macro that collides with a GDBusIntrospection struct
// member of the same name in glib. Undef it before pulling in glib/flatpak.
#ifdef signals
#undef signals
#endif

#include <QDebug>
#include <QCoreApplication>
#include <QThread>
#include <QMetaType>
#include <QVariantMap>
#include <glib.h>
#include <gio/gio.h>
#include <flatpak.h>

class FlatpakWorkerPrivate
{
public:
    explicit FlatpakWorkerPrivate(FlatpakWorker *qq)
        : q(qq)
    {
    }

    FlatpakInstallation *installation();
    void setProgress(double value, const QString &status);
    static void progressCallback(const char *status, unsigned int progress,
                                 gboolean estimating, gpointer userData);

    AppItem *makeInstalledItem(FlatpakInstalledRef *ref) const;
    AppItem *makeRemoteItem(FlatpakRemoteRef *ref, const QString &remote) const;

    // Items are created on the worker thread; QML binds to their properties
    // from the GUI thread, so hand ownership over to the main thread before
    // the queued signal delivers them.
    static void adoptToMainThread(const QList<AppItem *> &items);

    // <kind>/<name>/<arch>/<branch> e.g. app/org.gnome.Calculator/x86_64/stable
    static QString refString(const QString &kind, const QString &name,
                             const QString &arch, const QString &branch);

    FlatpakWorker *q;
    FlatpakInstallation *inst = nullptr;
    GCancellable *cancellable = nullptr;

    mutable QMutex mutex;
    double progressValue = 0.0;
    QString statusText;
};

FlatpakInstallation *FlatpakWorkerPrivate::installation()
{
    if (!inst) {
        g_autoptr(GError) error = nullptr;
        inst = flatpak_installation_new_user(nullptr, &error);
        if (!inst) {
            qWarning() << "flatpak: cannot open user installation:"
                       << (error ? error->message : "unknown");
            return nullptr;
        }
    }
    return inst;
}

void FlatpakWorkerPrivate::setProgress(double value, const QString &status)
{
    {
        QMutexLocker locker(&mutex);
        progressValue = value;
        if (!status.isEmpty())
            statusText = status;
    }
    const auto v = value;
    const auto s = status;
    QMetaObject::invokeMethod(q, [q = this->q, v, s] {
        Q_EMIT q->progressChanged(v);
        if (!s.isEmpty())
            Q_EMIT q->statusChanged(s);
    }, Qt::QueuedConnection);
}

void FlatpakWorkerPrivate::progressCallback(const char *status, unsigned int progress,
                                            gboolean estimating, gpointer userData)
{
    FlatpakWorkerPrivate *d = static_cast<FlatpakWorkerPrivate *>(userData);
    const QString s = status ? QString::fromUtf8(status) : QString();
    d->setProgress(progress, s);
}

QString FlatpakWorkerPrivate::refString(const QString &kind, const QString &name,
                                        const QString &arch, const QString &branch)
{
    return kind + QLatin1Char('/') + name + QLatin1Char('/') + arch + QLatin1Char('/') + branch;
}

void FlatpakWorkerPrivate::adoptToMainThread(const QList<AppItem *> &items)
{
    QThread *mainThread = QCoreApplication::instance()->thread();
    for (AppItem *item : items) {
        if (item && item->thread() != mainThread)
            item->moveToThread(mainThread);
    }
}

AppItem *FlatpakWorkerPrivate::makeInstalledItem(FlatpakInstalledRef *ref) const
{
    const QString appId = QString::fromUtf8(flatpak_ref_get_name(FLATPAK_REF(ref)));
    const QString branch = QString::fromUtf8(flatpak_ref_get_branch(FLATPAK_REF(ref)));
    const QString arch = QString::fromUtf8(flatpak_ref_get_arch(FLATPAK_REF(ref)));
    const QString origin = QString::fromUtf8(flatpak_installed_ref_get_origin(ref));
    const FlatpakRefKind kind = flatpak_ref_get_kind(FLATPAK_REF(ref));
    const bool isRuntime = (kind == FLATPAK_REF_KIND_RUNTIME);

    AppItem *item = new AppItem(appId, origin, branch, arch, isRuntime);
    item->setInstalled(true);
    item->setInstalledSize(flatpak_installed_ref_get_installed_size(ref));

    const char *version = flatpak_installed_ref_get_appdata_version(ref);
    if (version)
        item->setVersion(QString::fromUtf8(version));
    const char *summary = flatpak_installed_ref_get_appdata_summary(ref);
    if (summary)
        item->setSummary(QString::fromUtf8(summary));
    const char *license = flatpak_installed_ref_get_appdata_license(ref);
    if (license)
        item->setLicense(QString::fromUtf8(license));

    g_autoptr(GBytes) metadata = flatpak_installed_ref_load_metadata(ref, nullptr, nullptr);
    if (metadata) {
        g_autoptr(GVariant) variant = g_variant_new_from_bytes(
            G_VARIANT_TYPE("a{sv}"), metadata, FALSE);
        GVariant *runtimeV = nullptr;
        if (variant)
            runtimeV = g_variant_lookup_value(variant, "runtime", G_VARIANT_TYPE("(ss)"));
        if (runtimeV) {
            const gchar *runtimeName = nullptr;
            g_variant_get(runtimeV, "(&s&s)", &runtimeName, nullptr);
            item->setRuntime(QString::fromUtf8(runtimeName));
            g_variant_unref(runtimeV);
        }
    }

    return item;
}

AppItem *FlatpakWorkerPrivate::makeRemoteItem(FlatpakRemoteRef *ref, const QString &remote) const
{
    const QString appId = QString::fromUtf8(flatpak_ref_get_name(FLATPAK_REF(ref)));
    const QString branch = QString::fromUtf8(flatpak_ref_get_branch(FLATPAK_REF(ref)));
    const QString arch = QString::fromUtf8(flatpak_ref_get_arch(FLATPAK_REF(ref)));
    const FlatpakRefKind kind = flatpak_ref_get_kind(FLATPAK_REF(ref));
    const bool isRuntime = (kind == FLATPAK_REF_KIND_RUNTIME);

    AppItem *item = new AppItem(appId, remote, branch, arch, isRuntime);
    item->setName(appId);
    item->setInstalled(false);
    item->setInstalledSize(flatpak_remote_ref_get_installed_size(ref));
    item->setDownloadSize(flatpak_remote_ref_get_download_size(ref));
    return item;
}

FlatpakWorker::FlatpakWorker(QObject *parent)
    : QObject(parent)
    , d(new FlatpakWorkerPrivate(this))
{
}

FlatpakWorker::~FlatpakWorker()
{
    if (d->cancellable)
        g_object_unref(d->cancellable);
    if (d->inst)
        g_object_unref(d->inst);
}

double FlatpakWorker::progressValue() const
{
    QMutexLocker locker(&d->mutex);
    return d->progressValue;
}

QString FlatpakWorker::progressStatus() const
{
    QMutexLocker locker(&d->mutex);
    return d->statusText;
}

void FlatpakWorker::requestCancel()
{
    if (d->cancellable)
        g_cancellable_cancel(d->cancellable);
}

void FlatpakWorker::loadInstalled()
{
    FlatpakInstallation *inst = d->installation();
    if (!inst) {
        Q_EMIT installedRefsReady({});
        return;
    }

    g_autoptr(GError) error = nullptr;
    g_autoptr(GPtrArray) refs = flatpak_installation_list_installed_refs(inst, nullptr, &error);
    if (!refs) {
        qWarning() << "flatpak: list installed refs failed:"
                   << (error ? error->message : "unknown");
        Q_EMIT installedRefsReady({});
        return;
    }

    QList<AppItem *> items;
    items.reserve(refs->len);
    for (guint i = 0; i < refs->len; ++i) {
        FlatpakInstalledRef *ref = static_cast<FlatpakInstalledRef *>(g_ptr_array_index(refs, i));
        if (!ref)
            continue;
        items.append(d->makeInstalledItem(ref));
    }
    FlatpakWorkerPrivate::adoptToMainThread(items);
    Q_EMIT installedRefsReady(items);
}

void FlatpakWorker::loadRemotes()
{
    FlatpakInstallation *inst = d->installation();
    if (!inst) {
        Q_EMIT remotesReady({});
        return;
    }

    g_autoptr(GError) error = nullptr;
    g_autoptr(GPtrArray) remotes = flatpak_installation_list_remotes(inst, nullptr, &error);
    if (!remotes) {
        qWarning() << "flatpak: list remotes failed:"
                   << (error ? error->message : "unknown");
        Q_EMIT remotesReady({});
        return;
    }

    QVariantList list;
    list.reserve(static_cast<int>(remotes->len));
    for (guint i = 0; i < remotes->len; ++i) {
        FlatpakRemote *remote = static_cast<FlatpakRemote *>(g_ptr_array_index(remotes, i));
        if (!remote)
            continue;

        g_autofree char *url = flatpak_remote_get_url(remote);
        g_autofree char *title = flatpak_remote_get_title(remote);
        g_autofree char *comment = flatpak_remote_get_comment(remote);
        g_autofree char *description = flatpak_remote_get_description(remote);
        g_autofree char *homepage = flatpak_remote_get_homepage(remote);

        QVariantMap map;
        map.insert(QStringLiteral("name"),
                   QString::fromUtf8(flatpak_remote_get_name(remote)));
        map.insert(QStringLiteral("url"), url ? QString::fromUtf8(url) : QString());
        map.insert(QStringLiteral("title"), title ? QString::fromUtf8(title) : QString());
        map.insert(QStringLiteral("comment"), comment ? QString::fromUtf8(comment) : QString());
        map.insert(QStringLiteral("description"),
                   description ? QString::fromUtf8(description) : QString());
        map.insert(QStringLiteral("homepage"), homepage ? QString::fromUtf8(homepage) : QString());
        map.insert(QStringLiteral("disabled"), bool(flatpak_remote_get_disabled(remote)));
        map.insert(QStringLiteral("gpgVerify"), bool(flatpak_remote_get_gpg_verify(remote)));
        map.insert(QStringLiteral("noEnumerate"), bool(flatpak_remote_get_noenumerate(remote)));
        map.insert(QStringLiteral("prio"), flatpak_remote_get_prio(remote));
        list.append(map);
    }
    Q_EMIT remotesReady(list);
}

void FlatpakWorker::addRemote(const QString &name, const QString &url, bool gpgVerify)
{
    FlatpakInstallation *inst = d->installation();
    if (!inst) {
        Q_EMIT remoteOperationFinished(false, QStringLiteral("add"), name,
                                       QStringLiteral("Cannot open user installation"));
        return;
    }

    g_autoptr(GError) error = nullptr;
    g_autoptr(FlatpakRemote) remote = nullptr;

    // A ".flatpakrepo" URL is a small INI description of a repository: fetch it
    // and let libflatpak build a fully configured remote (real repo URL, title,
    // GPG key, ...). This mirrors `flatpak remote-add <name> <url>`.
    if (url.endsWith(QLatin1String(".flatpakrepo"), Qt::CaseInsensitive)) {
        g_autoptr(GFile) file = g_file_new_for_uri(url.toUtf8().constData());
        g_autofree char *contents = nullptr;
        gsize length = 0;
        if (!g_file_load_contents(file, nullptr, &contents, &length, nullptr, &error)) {
            Q_EMIT remoteOperationFinished(
                false, QStringLiteral("add"), name,
                error ? QString::fromUtf8(error->message)
                      : QStringLiteral("Cannot download %1").arg(url));
            return;
        }
        g_autoptr(GBytes) bytes = g_bytes_new(contents, length);
        remote = flatpak_remote_new_from_file(name.toUtf8().constData(), bytes, &error);
    } else {
        remote = flatpak_remote_new(name.toUtf8().constData());
        if (remote) {
            flatpak_remote_set_url(remote, url.toUtf8().constData());
            flatpak_remote_set_gpg_verify(remote, gpgVerify ? TRUE : FALSE);
        }
    }

    if (!remote) {
        Q_EMIT remoteOperationFinished(false, QStringLiteral("add"), name,
                                       error ? QString::fromUtf8(error->message)
                                             : QStringLiteral("Invalid remote configuration"));
        return;
    }

    g_clear_error(&error);
    const gboolean ok = flatpak_installation_add_remote(inst, remote, TRUE, nullptr, &error);
    Q_EMIT remoteOperationFinished(ok, QStringLiteral("add"), name,
                                   error ? QString::fromUtf8(error->message) : QString());
}

void FlatpakWorker::modifyRemote(const QString &name, const QString &title, const QString &url,
                                 bool gpgVerify, bool noEnumerate, int priority)
{
    FlatpakInstallation *inst = d->installation();
    if (!inst) {
        Q_EMIT remoteOperationFinished(false, QStringLiteral("edit"), name,
                                       QStringLiteral("Cannot open user installation"));
        return;
    }

    g_autoptr(GError) error = nullptr;
    g_autoptr(FlatpakRemote) remote = flatpak_installation_get_remote_by_name(
        inst, name.toUtf8().constData(), nullptr, &error);
    if (!remote) {
        Q_EMIT remoteOperationFinished(false, QStringLiteral("edit"), name,
                                       error ? QString::fromUtf8(error->message)
                                             : QStringLiteral("Remote not found"));
        return;
    }

    flatpak_remote_set_title(remote, title.toUtf8().constData());
    flatpak_remote_set_url(remote, url.toUtf8().constData());
    flatpak_remote_set_gpg_verify(remote, gpgVerify ? TRUE : FALSE);
    flatpak_remote_set_noenumerate(remote, noEnumerate ? TRUE : FALSE);
    flatpak_remote_set_prio(remote, priority);

    const gboolean ok = flatpak_installation_modify_remote(inst, remote, nullptr, &error);
    Q_EMIT remoteOperationFinished(ok, QStringLiteral("edit"), name,
                                   error ? QString::fromUtf8(error->message) : QString());
}

void FlatpakWorker::removeRemote(const QString &name)
{
    FlatpakInstallation *inst = d->installation();
    if (!inst) {
        Q_EMIT remoteOperationFinished(false, QStringLiteral("remove"), name,
                                       QStringLiteral("Cannot open user installation"));
        return;
    }

    g_autoptr(GError) error = nullptr;
    const gboolean ok =
        flatpak_installation_remove_remote(inst, name.toUtf8().constData(), nullptr, &error);
    Q_EMIT remoteOperationFinished(ok, QStringLiteral("remove"), name,
                                   error ? QString::fromUtf8(error->message) : QString());
}

void FlatpakWorker::setRemoteEnabled(const QString &name, bool enabled)
{
    FlatpakInstallation *inst = d->installation();
    if (!inst) {
        Q_EMIT remoteOperationFinished(false, QStringLiteral("modify"), name,
                                       QStringLiteral("Cannot open user installation"));
        return;
    }

    g_autoptr(GError) error = nullptr;
    g_autoptr(FlatpakRemote) remote = flatpak_installation_get_remote_by_name(
        inst, name.toUtf8().constData(), nullptr, &error);
    if (!remote) {
        Q_EMIT remoteOperationFinished(false, QStringLiteral("modify"), name,
                                       error ? QString::fromUtf8(error->message)
                                             : QStringLiteral("Remote not found"));
        return;
    }

    flatpak_remote_set_disabled(remote, enabled ? FALSE : TRUE);
    const gboolean ok = flatpak_installation_modify_remote(inst, remote, nullptr, &error);
    Q_EMIT remoteOperationFinished(ok, QStringLiteral("modify"), name,
                                   error ? QString::fromUtf8(error->message) : QString());
}

void FlatpakWorker::loadUpdates()
{
    FlatpakInstallation *inst = d->installation();
    if (!inst) {
        Q_EMIT updatesRefsReady({});
        return;
    }

    g_autoptr(GError) error = nullptr;
    g_autoptr(GPtrArray) refs = flatpak_installation_list_installed_refs_for_update(
        inst, nullptr, &error);
    if (!refs) {
        qWarning() << "flatpak: list updates failed:"
                   << (error ? error->message : "unknown");
        Q_EMIT updatesRefsReady({});
        return;
    }

    QList<AppItem *> items;
    items.reserve(refs->len);
    for (guint i = 0; i < refs->len; ++i) {
        FlatpakInstalledRef *ref = static_cast<FlatpakInstalledRef *>(g_ptr_array_index(refs, i));
        if (!ref)
            continue;
        AppItem *item = d->makeInstalledItem(ref);
        item->setUpdateAvailable(true);
        items.append(item);
    }
    FlatpakWorkerPrivate::adoptToMainThread(items);
    Q_EMIT updatesRefsReady(items);
}

void FlatpakWorker::loadRemote(const QString &remote)
{
    FlatpakInstallation *inst = d->installation();
    if (!inst) {
        Q_EMIT remoteRefsReady(remote, {});
        return;
    }

    g_autoptr(GError) error = nullptr;
    // Prefer the locally cached summary: it loads instantly and avoids a
    // network round-trip on every startup (Discover behaves the same way).
    // Fall back to a live refresh only when no cache exists yet (first run).
    g_autoptr(GPtrArray) refs = flatpak_installation_list_remote_refs_sync_full(
        inst, remote.toUtf8().constData(), FLATPAK_QUERY_FLAGS_ONLY_CACHED, nullptr, &error);
    if (!refs || refs->len == 0) {
        if (error)
            qWarning() << "flatpak: cached refs unavailable for" << remote
                       << ":" << (error ? error->message : "empty result");
        if (error)
            g_clear_error(&error);
        refs = flatpak_installation_list_remote_refs_sync_full(
            inst, remote.toUtf8().constData(), FLATPAK_QUERY_FLAGS_NONE, nullptr, &error);
    }
    if (!refs) {
        qWarning() << "flatpak: list remote refs failed for" << remote << ":"
                   << (error ? error->message : "unknown");
        Q_EMIT remoteRefsReady(remote, {});
        return;
    }

    QList<AppItem *> items;
    items.reserve(refs->len);
    for (guint i = 0; i < refs->len; ++i) {
        FlatpakRemoteRef *ref = static_cast<FlatpakRemoteRef *>(g_ptr_array_index(refs, i));
        if (!ref)
            continue;
        if (flatpak_ref_get_kind(FLATPAK_REF(ref)) == FLATPAK_REF_KIND_APP) {
            const QString appId = QString::fromUtf8(flatpak_ref_get_name(FLATPAK_REF(ref)));
            // BaseApps are private helper runtimes shipped with an app, not
            // standalone applications — hide them from the store listing.
            if (appId.endsWith(QLatin1String(".BaseApp")))
                continue;
            items.append(d->makeRemoteItem(ref, remote));
        }
    }
    FlatpakWorkerPrivate::adoptToMainThread(items);
    Q_EMIT remoteRefsReady(remote, items);
}

void FlatpakWorker::install(const QString &remote, const QString &kind, const QString &name,
                            const QString &arch, const QString &branch)
{
    FlatpakInstallation *inst = d->installation();
    const QString ref = FlatpakWorkerPrivate::refString(kind, name, arch, branch);
    if (!inst) {
        Q_EMIT operationFinished(ref, false, QStringLiteral("Cannot open user installation"));
        return;
    }

    if (d->cancellable)
        g_object_unref(d->cancellable);
    d->cancellable = g_cancellable_new();

    const FlatpakRefKind refKind =
        (kind == QLatin1String("runtime")) ? FLATPAK_REF_KIND_RUNTIME : FLATPAK_REF_KIND_APP;

    g_autoptr(GError) error = nullptr;
    g_autoptr(FlatpakInstalledRef) result = flatpak_installation_install_full(
        inst, FLATPAK_INSTALL_FLAGS_NONE, remote.toUtf8().constData(), refKind,
        name.toUtf8().constData(), arch.toUtf8().constData(), branch.toUtf8().constData(),
        nullptr, FlatpakWorkerPrivate::progressCallback, d.get(), d->cancellable, &error);

    const bool ok = (result != nullptr);
    const QString err = error ? QString::fromUtf8(error->message)
                              : (g_cancellable_is_cancelled(d->cancellable)
                                     ? QStringLiteral("Cancelled")
                                     : QString());
    Q_EMIT operationFinished(ref, ok, err);
}

void FlatpakWorker::uninstall(const QString &kind, const QString &name,
                              const QString &arch, const QString &branch)
{
    FlatpakInstallation *inst = d->installation();
    const QString ref = FlatpakWorkerPrivate::refString(kind, name, arch, branch);
    if (!inst) {
        Q_EMIT operationFinished(ref, false, QStringLiteral("Cannot open user installation"));
        return;
    }

    if (d->cancellable)
        g_object_unref(d->cancellable);
    d->cancellable = g_cancellable_new();

    const FlatpakRefKind refKind =
        (kind == QLatin1String("runtime")) ? FLATPAK_REF_KIND_RUNTIME : FLATPAK_REF_KIND_APP;

    g_autoptr(GError) error = nullptr;
    gboolean ok = flatpak_installation_uninstall_full(
        inst, FLATPAK_UNINSTALL_FLAGS_NONE, refKind, name.toUtf8().constData(),
        arch.toUtf8().constData(), branch.toUtf8().constData(),
        FlatpakWorkerPrivate::progressCallback, d.get(), d->cancellable, &error);

    const QString err = error ? QString::fromUtf8(error->message) : QString();
    Q_EMIT operationFinished(ref, ok, err);
}

void FlatpakWorker::updateApp(const QString &kind, const QString &name,
                              const QString &arch, const QString &branch)
{
    FlatpakInstallation *inst = d->installation();
    const QString ref = FlatpakWorkerPrivate::refString(kind, name, arch, branch);
    if (!inst) {
        Q_EMIT operationFinished(ref, false, QStringLiteral("Cannot open user installation"));
        return;
    }

    if (d->cancellable)
        g_object_unref(d->cancellable);
    d->cancellable = g_cancellable_new();

    const FlatpakRefKind refKind =
        (kind == QLatin1String("runtime")) ? FLATPAK_REF_KIND_RUNTIME : FLATPAK_REF_KIND_APP;

    g_autoptr(GError) error = nullptr;
    g_autoptr(FlatpakInstalledRef) result = flatpak_installation_update_full(
        inst, FLATPAK_UPDATE_FLAGS_NONE, refKind, name.toUtf8().constData(),
        arch.toUtf8().constData(), branch.toUtf8().constData(), nullptr,
        FlatpakWorkerPrivate::progressCallback, d.get(), d->cancellable, &error);

    const bool ok = (result != nullptr);
    const QString err = error ? QString::fromUtf8(error->message) : QString();
    Q_EMIT operationFinished(ref, ok, err);
}

void FlatpakWorker::updateAll()
{
    FlatpakInstallation *inst = d->installation();
    if (!inst) {
        Q_EMIT operationFinished(QString(), false, QStringLiteral("Cannot open user installation"));
        return;
    }

    if (d->cancellable)
        g_object_unref(d->cancellable);
    d->cancellable = g_cancellable_new();

    g_autoptr(GError) error = nullptr;
    g_autoptr(GPtrArray) refs = flatpak_installation_list_installed_refs_for_update(
        inst, nullptr, &error);
    if (!refs) {
        const QString err = error ? QString::fromUtf8(error->message) : QString();
        Q_EMIT operationFinished(QString(), false, err);
        return;
    }

    bool anyFailed = false;
    for (guint i = 0; i < refs->len; ++i) {
        FlatpakInstalledRef *ref = static_cast<FlatpakInstalledRef *>(g_ptr_array_index(refs, i));
        if (!ref)
            continue;
        const QString kind = flatpak_ref_get_kind(FLATPAK_REF(ref)) == FLATPAK_REF_KIND_RUNTIME
                                 ? QStringLiteral("runtime") : QStringLiteral("app");
        const QString name = QString::fromUtf8(flatpak_ref_get_name(FLATPAK_REF(ref)));
        const QString arch = QString::fromUtf8(flatpak_ref_get_arch(FLATPAK_REF(ref)));
        const QString branch = QString::fromUtf8(flatpak_ref_get_branch(FLATPAK_REF(ref)));

        g_autoptr(GError) opError = nullptr;
        g_autoptr(FlatpakInstalledRef) result = flatpak_installation_update_full(
            inst, FLATPAK_UPDATE_FLAGS_NONE,
            flatpak_ref_get_kind(FLATPAK_REF(ref)), name.toUtf8().constData(),
            arch.toUtf8().constData(), branch.toUtf8().constData(), nullptr,
            FlatpakWorkerPrivate::progressCallback, d.get(), d->cancellable, &opError);

        if (!result) {
            anyFailed = true;
            qWarning() << "flatpak: update failed for" << name << ":"
                       << (opError ? opError->message : "unknown");
        }
        if (g_cancellable_is_cancelled(d->cancellable))
            break;
    }

    Q_EMIT operationFinished(QString(), !anyFailed, QString());
}