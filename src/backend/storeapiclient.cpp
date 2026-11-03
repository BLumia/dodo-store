// SPDX-FileCopyrightText: 2026 dodo-store authors
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "storeapiclient.h"

#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QCryptographicHash>
#include <QLocale>
#include <QDateTime>
#include <QDebug>

namespace {

// One hour, as requested: newer caches are reused without a request.
constexpr qint64 kCacheTtlSeconds = 3600;

const QStringList kCollections = {
    QStringLiteral("trending"),
    QStringLiteral("popular"),
    QStringLiteral("recently-added"),
    QStringLiteral("recently-updated"),
};

QNetworkProxy proxyFromString(const QString &value)
{
    const QString text = value.trimmed();
    if (text.isEmpty())
        return QNetworkProxy(QNetworkProxy::NoProxy);

    QUrl url(text);
    if (url.scheme().isEmpty())
        url = QUrl(QStringLiteral("http://") + text);

    const QString scheme = url.scheme().toLower();
    QNetworkProxy::ProxyType type;
    if (scheme == QLatin1String("http") || scheme == QLatin1String("https"))
        type = QNetworkProxy::HttpProxy;
    else if (scheme == QLatin1String("socks5") || scheme == QLatin1String("socks5h"))
        type = QNetworkProxy::Socks5Proxy;
    else
        return QNetworkProxy(QNetworkProxy::NoProxy);

    if (url.host().isEmpty() || url.port() <= 0)
        return QNetworkProxy(QNetworkProxy::NoProxy);

    qDebug() << "store api: using proxy" << type << url.host() << url.port();
    return QNetworkProxy(type, url.host(), url.port());
}

} // namespace

StoreApiClient::StoreApiClient(QObject *parent)
    : QObject(parent)
    , m_manager(new QNetworkAccessManager(this))
{
}

void StoreApiClient::configure(const QString &baseUrl, const QString &proxy)
{
    QString base = baseUrl.trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    m_baseUrl = base;
    m_manager->setProxy(proxyFromString(proxy));
}

void StoreApiClient::refresh(bool force)
{
    // Invalidate any request still in flight; its reply is ignored below.
    ++m_generation;
    const int generation = m_generation;
    for (QNetworkReply *reply : m_replies)
        reply->abort();
    m_replies.clear();
    m_pending = 0;

    if (m_baseUrl.isEmpty()) {
        Q_EMIT refreshStarted();
        for (const QString &collection : kCollections)
            Q_EMIT collectionFailed(collection, tr("Store API address is empty"),
                                    false, {});
        Q_EMIT refreshFinished();
        return;
    }

    Q_EMIT refreshStarted();
    m_pending = kCollections.size();
    qDebug() << "store api: refresh base=" << m_baseUrl << "force=" << force;
    const QString locale = QLocale::system().name();

    for (const QString &collection : kCollections) {
        const CacheEntry cache = readCache(collection);
        const qint64 age = QDateTime::currentSecsSinceEpoch() - cache.fetchedAt;
        if (!force && cache.valid && age >= 0 && age < kCacheTtlSeconds) {
            Q_EMIT collectionReady(collection, cache.apps);
            finishOne();
            continue;
        }

        QUrl url(m_baseUrl + QStringLiteral("/collection/") + collection);
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("page"), QStringLiteral("1"));
        query.addQueryItem(QStringLiteral("per_page"), QStringLiteral("24"));
        query.addQueryItem(QStringLiteral("locale"), locale);
        url.setQuery(query);

        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setTransferTimeout(15000);

        QNetworkReply *reply = m_manager->get(request);
        m_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this,
                [this, generation, collection, reply, cache] {
            if (generation != m_generation) {
                reply->deleteLater();
                return;
            }
            m_replies.removeAll(reply);
            reply->deleteLater();

            if (reply->error() != QNetworkReply::NoError) {
                qWarning() << "store api: failed" << collection << reply->errorString();
                Q_EMIT collectionFailed(collection, reply->errorString(),
                                        cache.valid, cache.apps);
            } else {
                const QByteArray data = reply->readAll();
                QJsonParseError parseError;
                const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
                if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                    const QString message =
                        parseError.error != QJsonParseError::NoError
                            ? parseError.errorString()
                            : tr("Invalid store API response");
                    Q_EMIT collectionFailed(collection, message, cache.valid, cache.apps);
                } else {
                    const QJsonArray hits =
                        doc.object().value(QStringLiteral("hits")).toArray();
                    const QVariantList apps = hits.toVariantList();
                    qDebug() << "store api: ok" << collection << apps.size();
                    writeCache(collection, apps);
                    Q_EMIT collectionReady(collection, apps);
                }
            }
            finishOne();
        });
    }
}

void StoreApiClient::finishOne()
{
    if (m_pending <= 0)
        return;
    --m_pending;
    if (m_pending == 0)
        Q_EMIT refreshFinished();
}

QString StoreApiClient::cacheDir() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QByteArray hash =
        QCryptographicHash::hash(m_baseUrl.toUtf8(), QCryptographicHash::Sha1).toHex();
    return base + QStringLiteral("/storeapi/") + QString::fromLatin1(hash);
}

StoreApiClient::CacheEntry StoreApiClient::readCache(const QString &collection) const
{
    CacheEntry entry;

    QFile file(cacheDir() + QLatin1Char('/') + collection + QStringLiteral(".json"));
    if (!file.open(QIODevice::ReadOnly))
        return entry;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return entry;

    const QJsonObject object = doc.object();
    entry.fetchedAt = static_cast<qint64>(
        object.value(QStringLiteral("fetchedAt")).toDouble());
    entry.apps = object.value(QStringLiteral("apps")).toArray().toVariantList();
    entry.valid = true;
    return entry;
}

void StoreApiClient::writeCache(const QString &collection, const QVariantList &apps) const
{
    QDir dir(cacheDir());
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return;

    QJsonObject object;
    object.insert(QStringLiteral("fetchedAt"),
                  static_cast<double>(QDateTime::currentSecsSinceEpoch()));
    object.insert(QStringLiteral("apps"), QJsonArray::fromVariantList(apps));

    QFile file(dir.filePath(collection + QStringLiteral(".json")));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
}
