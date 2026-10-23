// SPDX-FileCopyrightText: 2026 dodo-store authors
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QStringList>

#include <AppStreamQt/pool.h>

class AppItem;

// Wraps AppStreamQt's Pool for flatpak appstream metadata (names, summaries,
// descriptions, icons, screenshots). Lives on the GUI thread and loads
// asynchronously; once loaded it can enrich AppItem instances by app id.
class AppStreamPoolBridge : public QObject
{
    Q_OBJECT
public:
    explicit AppStreamPoolBridge(QObject *parent = nullptr);

    void startLoading();
    bool loaded() const;

    // Enrich the given item from the pool. Returns true if any field changed.
    bool enrich(AppItem *item);

    // App ids of the components carrying @p category (an AppStream category id
    // such as "Game"). Used by the category view of the browse page.
    QStringList appIdsInCategory(const QString &category) const;

Q_SIGNALS:
    void loadFinished(bool success);

private:
    AppStream::Pool m_pool;
    bool m_loaded = false;
};