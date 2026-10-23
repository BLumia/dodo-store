// SPDX-FileCopyrightText: 2026 dodo-store authors
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class QNetworkAccessManager;
class QNetworkReply;

// Fetches the curated app collections (trending/popular/new/updated) from a
// Flathub-API-compatible endpoint. Results are cached on disk for one hour;
// a failed refresh falls back to the cached data when available.
class StoreApiClient : public QObject
{
    Q_OBJECT
public:
    explicit StoreApiClient(QObject *parent = nullptr);

    // @p baseUrl e.g. "https://flathub.org/api/v2". @p proxy is empty (no
    // proxy) or "http://host:port" / "socks5://host:port".
    void configure(const QString &baseUrl, const QString &proxy);

    // force=false uses the cache while it is younger than one hour.
    void refresh(bool force = false);

Q_SIGNALS:
    void collectionReady(const QString &collection, const QVariantList &apps);
    // hadCache tells whether cachedApps holds usable data. It is empty when
    // hadCache is false.
    void collectionFailed(const QString &collection, const QString &error,
                          bool hadCache, const QVariantList &cachedApps);
    void refreshStarted();
    void refreshFinished();

private:
    struct CacheEntry {
        bool valid = false;
        qint64 fetchedAt = 0;
        QVariantList apps;
    };

    CacheEntry readCache(const QString &collection) const;
    void writeCache(const QString &collection, const QVariantList &apps) const;
    QString cacheDir() const;
    void finishOne();

    QNetworkAccessManager *m_manager = nullptr;
    QString m_baseUrl;
    QList<QNetworkReply *> m_replies;
    int m_pending = 0;
    int m_generation = 0;
};
