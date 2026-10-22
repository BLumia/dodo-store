// SPDX-FileCopyrightText: 2026 dodo-store authors
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "appfilterproxymodel.h"
#include "applistmodel.h"

AppFilterProxyModel::AppFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setFilterCaseSensitivity(Qt::CaseInsensitive);

    // Keep QML's "count" binding in sync as the filtered row set changes.
    connect(this, &QAbstractItemModel::rowsInserted,
            this, &AppFilterProxyModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved,
            this, &AppFilterProxyModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset,
            this, &AppFilterProxyModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged,
            this, &AppFilterProxyModel::countChanged);
}

int AppFilterProxyModel::count() const
{
    return rowCount();
}

QString AppFilterProxyModel::filterText() const
{
    return m_filterText;
}

void AppFilterProxyModel::setFilterText(const QString &text)
{
    if (m_filterText == text)
        return;
    m_filterText = text;
    setFilterFixedString(text);
    Q_EMIT filterTextChanged();
}


bool AppFilterProxyModel::featuredOnly() const
{
    return m_featuredOnly;
}

void AppFilterProxyModel::setFeaturedOnly(bool enabled)
{
    if (m_featuredOnly == enabled)
        return;
    m_featuredOnly = enabled;
    invalidateFilter();
    Q_EMIT featuredOnlyChanged();
}

QStringList AppFilterProxyModel::featuredAppIds() const
{
    return m_featuredAppIds;
}

void AppFilterProxyModel::setFeaturedAppIds(const QStringList &appIds)
{
    if (m_featuredAppIds == appIds)
        return;
    m_featuredAppIds = appIds;
    invalidateFilter();
    Q_EMIT featuredAppIdsChanged();
}

bool AppFilterProxyModel::hideRuntimes() const
{
    return m_hideRuntimes;
}

void AppFilterProxyModel::setHideRuntimes(bool hide)
{
    if (m_hideRuntimes == hide)
        return;
    m_hideRuntimes = hide;
    invalidateFilter();
    Q_EMIT hideRuntimesChanged();
}

bool AppFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);

    if (m_hideRuntimes && idx.data(AppListModel::IsRuntimeRole).toBool())
        return false;

    // Searching always covers the full source catalog, not just featured apps.
    if (!m_filterText.trimmed().isEmpty()) {
        const QString appId = idx.data(AppListModel::AppIdRole).toString();
        const QString name = idx.data(AppListModel::NameRole).toString();
        const QString summary = idx.data(AppListModel::SummaryRole).toString();

        return appId.contains(m_filterText, Qt::CaseInsensitive)
               || name.contains(m_filterText, Qt::CaseInsensitive)
               || summary.contains(m_filterText, Qt::CaseInsensitive);
    }

    if (m_featuredOnly) {
        const QString appId = idx.data(AppListModel::AppIdRole).toString();
        return m_featuredAppIds.contains(appId);
    }

    return true;
}

}
