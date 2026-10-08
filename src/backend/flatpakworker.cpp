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
#include <QSet>
#include <QVariantMap>
#include <QUrl>
#include <QRegularExpression>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QEventLoop>
#include <QTimer>
#include <QFile>
#include <glib.h>
#include <gio/gio.h>
#include <flatpak.h>

namespace {

// Synchronously fetches @p url on the current (worker) thread. http/https go
// through Qt Network and file:// paths through QFile — deliberately not GIO.
// Apps launched by xdg-desktop-portal inherit GIO_USE_VFS=local, which makes
// GIO's remote schemes (GVfs) unavailable, so g_file_load_contents() would fail
// with "operation not supported" for exactly the flatpak+https links the portal
// hands us.
bool readUri(const QString &url, QByteArray *out, QString *error)
{
    const QUrl u(url);
    const QString scheme = u.scheme().toLower();

    if (scheme.isEmpty() || scheme == QLatin1String("file")) {
        const QString path = u.isLocalFile() ? u.toLocalFile() : url;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error)
                *error = file.errorString();
            return false;
        }
        *out = file.readAll();
        return true;
    }

    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) {
        if (error)
            *error = QStringLiteral("Unsupported URL scheme \"%1\"").arg(scheme);
        return false;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(u);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = manager.get(request);

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(30000);
    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
        if (error)
            *error = QStringLiteral("Timed out downloading %1").arg(url);
        reply->deleteLater();
        return false;
    }
    if (reply->error() != QNetworkReply::NoError) {
        if (error)
            *error = reply->errorString();
        reply->deleteLater();
        return false;
    }
    *out = reply->readAll();
    reply->deleteLater();
    return true;
}

} // namespace

class FlatpakWorkerPrivate
{
public:
    explicit FlatpakWorkerPrivate(FlatpakWorker *qq)
        : q(qq)
    {
    }

    FlatpakInstallation *installation();
    void setProgress(double value, const QString &status);
    GCancellable *resetCancellable();

    // Runs the prepared transaction and reports the overall result: success
    // only when every operation (the requested ref plus any dependencies the
    // transaction added) completed without error.
    bool runTransaction(FlatpakTransaction *transaction, QString *errorOut);

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

    // State of the transaction currently being run (worker thread only).
    QSet<QString> failedRefs;
    QString lastOpError;

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

GCancellable *FlatpakWorkerPrivate::resetCancellable()
{
    if (cancellable)
        g_object_unref(cancellable);
    cancellable = g_cancellable_new();
    return cancellable;
}

// ---------------------------------------------------------------------------
// FlatpakTransaction plumbing
//
// Install/update operations must go through FlatpakTransaction. The old
// flatpak_installation_install_full()/update_full() APIs deploy only the
// requested ref and never resolve its dependencies, leaving freshly installed
// applications without their runtime (i.e. they cannot run). A transaction
// resolves the ref's runtime, related refs etc. and queues them as extra
// operations, all covered by one progress/error stream.
// ---------------------------------------------------------------------------

namespace {

void transactionProgressChanged(FlatpakTransactionProgress *progress, gpointer userData)
{
    auto *d = static_cast<FlatpakWorkerPrivate *>(userData);
    g_autofree char *status = flatpak_transaction_progress_get_status(progress);
    const int percent = flatpak_transaction_progress_get_progress(progress);
    d->setProgress(percent, status ? QString::fromUtf8(status) : QString());
}

void transactionNewOperation(FlatpakTransaction *transaction,
                             FlatpakTransactionOperation *operation,
                             FlatpakTransactionProgress *progress, gpointer userData)
{
    Q_UNUSED(transaction);
    auto *d = static_cast<FlatpakWorkerPrivate *>(userData);
    // A transaction usually runs several operations (the requested ref plus
    // its runtime and related refs); announce which one is starting.
    const QString ref = QString::fromUtf8(flatpak_transaction_operation_get_ref(operation));
    d->setProgress(0, ref);
    g_signal_connect(progress, "changed", G_CALLBACK(transactionProgressChanged), d);
}

gboolean transactionOperationError(FlatpakTransaction *transaction,
                                   FlatpakTransactionOperation *operation,
                                   const GError *error, guint details, gpointer userData)
{
    Q_UNUSED(transaction);
    Q_UNUSED(details);
    auto *d = static_cast<FlatpakWorkerPrivate *>(userData);
    const QString ref = QString::fromUtf8(flatpak_transaction_operation_get_ref(operation));
    d->failedRefs.insert(ref);
    d->lastOpError = error ? QString::fromUtf8(error->message)
                           : QStringLiteral("unknown error");
    qWarning() << "flatpak: transaction operation failed for" << ref << ":" << d->lastOpError;
    // Keep going (updateAll batches several refs); the overall result is
    // computed from failedRefs once the transaction finishes.
    return TRUE;
}

gboolean transactionReady(FlatpakTransaction *transaction, gpointer userData)
{
    Q_UNUSED(transaction);
    Q_UNUSED(userData);
    return TRUE; // nothing to confirm, run the prepared operation list
}

} // namespace

bool FlatpakWorkerPrivate::runTransaction(FlatpakTransaction *transaction, QString *errorOut)
{
    failedRefs.clear();
    lastOpError.clear();
    const gulong newOpHandler = g_signal_connect(transaction, "new-operation",
                                                 G_CALLBACK(transactionNewOperation), this);
    const gulong errorHandler = g_signal_connect(transaction, "operation-error",
                                                 G_CALLBACK(transactionOperationError), this);
    const gulong readyHandler = g_signal_connect(transaction, "ready",
                                                 G_CALLBACK(transactionReady), this);

    g_autoptr(GError) error = nullptr;
    const bool ran = flatpak_transaction_run(transaction, cancellable, &error);

    g_signal_handler_disconnect(transaction, newOpHandler);
    g_signal_handler_disconnect(transaction, errorHandler);
    g_signal_handler_disconnect(transaction, readyHandler);

    const bool cancelled = g_cancellable_is_cancelled(cancellable);
    if (errorOut) {
        if (cancelled)
            *errorOut = QStringLiteral("Cancelled");
        else if (error)
            *errorOut = QString::fromUtf8(error->message);
        else if (!lastOpError.isEmpty())
            *errorOut = lastOpError;
    }
    return ran && !cancelled && failedRefs.isEmpty();
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
        QByteArray contents;
        QString fetchError;
        if (!readUri(url, &contents, &fetchError)) {
            Q_EMIT remoteOperationFinished(
                false, QStringLiteral("add"), name,
                fetchError.isEmpty() ? QStringLiteral("Cannot download %1").arg(url)
                                     : fetchError);
            return;
        }
        g_autoptr(GBytes) bytes = g_bytes_new(contents.constData(), contents.size());
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

namespace {

// Repositories are compared with the trailing slash normalised away so
// "https://dl.flathub.org/repo/" and "https://dl.flathub.org/repo" match.
QString normalizeRepoUrl(const QString &url)
{
    QString s = url.trimmed();
    while (s.endsWith(QLatin1Char('/')))
        s.chop(1);
    return s;
}

// Returns the name of a configured remote matching the ref's repository URL,
// preferring @p suggestedName (typically "flathub") when it exists.
QString findRemoteForRef(FlatpakInstallation *inst, const QString &refUrl,
                         const QString &suggestedName)
{
    g_autoptr(GError) error = nullptr;
    g_autoptr(GPtrArray) remotes = flatpak_installation_list_remotes(inst, nullptr, &error);
    if (!remotes)
        return QString();

    if (!suggestedName.isEmpty()) {
        for (guint i = 0; i < remotes->len; ++i) {
            FlatpakRemote *r = static_cast<FlatpakRemote *>(g_ptr_array_index(remotes, i));
            if (r && suggestedName == QString::fromUtf8(flatpak_remote_get_name(r)))
                return suggestedName;
        }
    }

    const QString target = normalizeRepoUrl(refUrl);
    if (target.isEmpty())
        return QString();
    for (guint i = 0; i < remotes->len; ++i) {
        FlatpakRemote *r = static_cast<FlatpakRemote *>(g_ptr_array_index(remotes, i));
        if (!r)
            continue;
        g_autofree char *url = flatpak_remote_get_url(r);
        if (url && normalizeRepoUrl(QString::fromUtf8(url))
                       .compare(target, Qt::CaseInsensitive) == 0)
            return QString::fromUtf8(flatpak_remote_get_name(r));
    }
    return QString();
}

// Derives a usable remote name from a repository URL when the ref carries no
// SuggestRemoteName (e.g. "https://example.com/repo/" -> "example.com").
QString deriveRemoteName(const QString &url)
{
    QString host = QUrl(url).host();
    if (host.isEmpty())
        host = QStringLiteral("flatpak-ref");
    host.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")),
                 QStringLiteral("-"));
    return host;
}

} // namespace

void FlatpakWorker::resolveFlatpakRef(const QString &url)
{
    FlatpakInstallation *inst = d->installation();
    if (!inst) {
        Q_EMIT flatpakRefFailed(url, QStringLiteral("Cannot open user installation"));
        return;
    }

    g_autoptr(GError) error = nullptr;
    QByteArray contents;
    QString fetchError;
    if (!readUri(url, &contents, &fetchError)) {
        Q_EMIT flatpakRefFailed(url, fetchError.isEmpty()
                                         ? QStringLiteral("Cannot download %1").arg(url)
                                         : fetchError);
        return;
    }

    g_autoptr(GKeyFile) keyFile = g_key_file_new();
    if (!g_key_file_load_from_data(keyFile, contents.constData(), contents.size(),
                                   G_KEY_FILE_NONE, &error)) {
        Q_EMIT flatpakRefFailed(url, error ? QString::fromUtf8(error->message)
                                           : QStringLiteral("Invalid flatpakref: %1").arg(url));
        return;
    }

    g_autofree char *name = g_key_file_get_string(keyFile, "Flatpak Ref", "Name", nullptr);
    g_autofree char *branch = g_key_file_get_string(keyFile, "Flatpak Ref", "Branch", nullptr);
    g_autofree char *refUrl = g_key_file_get_string(keyFile, "Flatpak Ref", "Url", nullptr);
    g_autofree char *suggested =
        g_key_file_get_string(keyFile, "Flatpak Ref", "SuggestRemoteName", nullptr);
    g_autofree char *gpgKey = g_key_file_get_string(keyFile, "Flatpak Ref", "GPGKey", nullptr);

    if (!name || !refUrl) {
        Q_EMIT flatpakRefFailed(url, QStringLiteral("Flatpakref is missing Name or Url"));
        return;
    }

    const QString appId = QString::fromUtf8(name);
    const QString appBranch = branch ? QString::fromUtf8(branch) : QStringLiteral("master");
    const QString repoUrl = QString::fromUtf8(refUrl);
    const QString suggestedName = suggested ? QString::fromUtf8(suggested) : QString();

    QString remoteName = findRemoteForRef(inst, repoUrl, suggestedName);
    if (remoteName.isEmpty()) {
        remoteName = suggestedName.isEmpty() ? deriveRemoteName(repoUrl) : suggestedName;
        g_autoptr(FlatpakRemote) remote = flatpak_remote_new(remoteName.toUtf8().constData());
        if (!remote) {
            Q_EMIT flatpakRefFailed(url, QStringLiteral("Invalid remote configuration"));
            return;
        }
        flatpak_remote_set_url(remote, repoUrl.toUtf8().constData());
        // The ref carries its GPG key base64-encoded; libflatpak expects the
        // decoded bytes (cf. flatpak_dir_parse_flatpakref()).
        gboolean verify = FALSE;
        if (gpgKey) {
            gsize decodedLen = 0;
            g_autofree guchar *decoded = g_base64_decode(gpgKey, &decodedLen);
            if (decodedLen >= 10) {
                g_autoptr(GBytes) keyBytes = g_bytes_new(decoded, decodedLen);
                flatpak_remote_set_gpg_key(remote, keyBytes);
                verify = TRUE;
            }
        }
        flatpak_remote_set_gpg_verify(remote, verify);
        g_clear_error(&error);
        if (!flatpak_installation_add_remote(inst, remote, TRUE, nullptr, &error)) {
            Q_EMIT flatpakRefFailed(
                url, error ? QString::fromUtf8(error->message)
                           : QStringLiteral("Cannot add remote %1").arg(remoteName));
            return;
        }
    }

    Q_EMIT flatpakRefResolved(remoteName, appId, appBranch);
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

    d->resetCancellable();

    g_autoptr(GError) error = nullptr;
    g_autoptr(FlatpakTransaction) transaction =
        flatpak_transaction_new_for_installation(inst, d->cancellable, &error);
    if (!transaction) {
        Q_EMIT operationFinished(ref, false, QString::fromUtf8(error ? error->message : "unknown"));
        return;
    }

    // The transaction resolves the ref's runtime and other dependencies and
    // queues them as additional operations, so the installed app can run.
    if (!flatpak_transaction_add_install(transaction, remote.toUtf8().constData(),
                                         ref.toUtf8().constData(), nullptr, &error)) {
        Q_EMIT operationFinished(ref, false, QString::fromUtf8(error ? error->message : "unknown"));
        return;
    }

    QString err;
    const bool ok = d->runTransaction(transaction, &err);
    d->setProgress(100, QString());
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

    d->resetCancellable();

    g_autoptr(GError) error = nullptr;
    g_autoptr(FlatpakTransaction) transaction =
        flatpak_transaction_new_for_installation(inst, d->cancellable, &error);
    if (!transaction) {
        Q_EMIT operationFinished(ref, false, QString::fromUtf8(error ? error->message : "unknown"));
        return;
    }

    if (!flatpak_transaction_add_uninstall(transaction, ref.toUtf8().constData(), &error)) {
        Q_EMIT operationFinished(ref, false, QString::fromUtf8(error ? error->message : "unknown"));
        return;
    }

    QString err;
    const bool ok = d->runTransaction(transaction, &err);
    d->setProgress(100, QString());
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

    d->resetCancellable();

    g_autoptr(GError) error = nullptr;
    g_autoptr(FlatpakTransaction) transaction =
        flatpak_transaction_new_for_installation(inst, d->cancellable, &error);
    if (!transaction) {
        Q_EMIT operationFinished(ref, false, QString::fromUtf8(error ? error->message : "unknown"));
        return;
    }

    // Updating through a transaction also refreshes the ref's dependencies
    // (runtime updates, related refs) instead of only the ref itself.
    if (!flatpak_transaction_add_update(transaction, ref.toUtf8().constData(),
                                        nullptr, nullptr, &error)) {
        Q_EMIT operationFinished(ref, false, QString::fromUtf8(error ? error->message : "unknown"));
        return;
    }

    QString err;
    const bool ok = d->runTransaction(transaction, &err);
    d->setProgress(100, QString());
    Q_EMIT operationFinished(ref, ok, err);
}

void FlatpakWorker::updateAll()
{
    FlatpakInstallation *inst = d->installation();
    if (!inst) {
        Q_EMIT operationFinished(QString(), false, QStringLiteral("Cannot open user installation"));
        return;
    }

    d->resetCancellable();

    g_autoptr(GError) error = nullptr;
    g_autoptr(GPtrArray) refs = flatpak_installation_list_installed_refs_for_update(
        inst, nullptr, &error);
    if (!refs) {
        const QString err = error ? QString::fromUtf8(error->message) : QString();
        Q_EMIT operationFinished(QString(), false, err);
        return;
    }

    g_autoptr(FlatpakTransaction) transaction =
        flatpak_transaction_new_for_installation(inst, d->cancellable, &error);
    if (!transaction) {
        Q_EMIT operationFinished(QString(), false,
                                 QString::fromUtf8(error ? error->message : "unknown"));
        return;
    }

    // One transaction for the whole batch: dependencies are resolved once for
    // all refs and every ref failure is reported through the same handler.
    for (guint i = 0; i < refs->len; ++i) {
        FlatpakRef *ref = static_cast<FlatpakRef *>(g_ptr_array_index(refs, i));
        if (!ref)
            continue;
        g_autofree char *refStr = flatpak_ref_format_ref(ref);
        if (!flatpak_transaction_add_update(transaction, refStr, nullptr, nullptr, &error)) {
            Q_EMIT operationFinished(QString(), false,
                                     QString::fromUtf8(error ? error->message : "unknown"));
            return;
        }
    }

    QString err;
    bool ok = true;
    if (refs->len > 0)
        ok = d->runTransaction(transaction, &err);
    d->setProgress(100, QString());
    Q_EMIT operationFinished(QString(), ok, err);
}