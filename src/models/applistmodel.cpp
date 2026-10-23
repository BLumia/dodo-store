// SPDX-FileCopyrightText: 2026 dodo-store authors
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "applistmodel.h"
#include "appitem.h"

AppListModel::AppListModel(QObject *parent)
    : QAbstractListModel(parent)
{
    // Keep the QML-facing count in sync no matter how the model is mutated.
    connect(this, &QAbstractItemModel::rowsInserted,
            this, &AppListModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved,
            this, &AppListModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset,
            this, &AppListModel::countChanged);
}

int AppListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_items.size();
}

int AppListModel::count() const
{
    return m_items.size();
}

QVariant AppListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};

    AppItem *item = m_items.at(index.row());
    switch (role) {
    case AppIdRole: return item->appId();
    case NameRole: return item->name();
    case SummaryRole: return item->summary();
    case DescriptionRole: return item->description();
    case VersionRole: return item->version();
    case RemoteRole: return item->remote();
    case BranchRole: return item->branch();
    case ArchRole: return item->arch();
    case RuntimeRole: return item->runtime();
    case LicenseRole: return item->license();
    case IconRole: return item->iconUrl();
    case ScreenshotsRole: return item->screenshotsQml();
    case InstalledSizeRole: return item->installedSize();
    case DownloadSizeRole: return item->downloadSize();
    case InstalledRole: return item->installed();
    case UpdateAvailableRole: return item->updateAvailable();
    case IsRuntimeRole: return item->isRuntime();
    case ItemRole: return QVariant::fromValue(item);
    default: return {};
    }
}

QHash<int, QByteArray> AppListModel::roleNames() const
{
    return {
        { AppIdRole, "appId" },
        { NameRole, "name" },
        { SummaryRole, "summary" },
        { DescriptionRole, "description" },
        { VersionRole, "version" },
        { RemoteRole, "remote" },
        { BranchRole, "branch" },
        { ArchRole, "arch" },
        { RuntimeRole, "runtime" },
        { LicenseRole, "license" },
        { IconRole, "icon" },
        { ScreenshotsRole, "screenshots" },
        { InstalledSizeRole, "installedSize" },
        { DownloadSizeRole, "downloadSize" },
        { InstalledRole, "installed" },
        { UpdateAvailableRole, "updateAvailable" },
        { IsRuntimeRole, "isRuntime" },
        { ItemRole, "item" }
    };
}

void AppListModel::connectItemSignals(AppItem *item)
{
    if (!item)
        return;

    const auto notifyRole = [this, item](auto signal, int role) {
        connect(item, signal, this, [this, item, role] {
            const int row = m_items.indexOf(item);
            if (row < 0)
                return;
            const QModelIndex changedIndex = index(row, 0);
            Q_EMIT dataChanged(changedIndex, changedIndex, {role});
        });
    };

    notifyRole(&AppItem::nameChanged, NameRole);
    notifyRole(&AppItem::summaryChanged, SummaryRole);
    notifyRole(&AppItem::descriptionChanged, DescriptionRole);
    notifyRole(&AppItem::versionChanged, VersionRole);
    notifyRole(&AppItem::runtimeChanged, RuntimeRole);
    notifyRole(&AppItem::licenseChanged, LicenseRole);
    notifyRole(&AppItem::iconChanged, IconRole);
    notifyRole(&AppItem::screenshotsChanged, ScreenshotsRole);
    notifyRole(&AppItem::installedSizeChanged, InstalledSizeRole);
    notifyRole(&AppItem::downloadSizeChanged, DownloadSizeRole);
    notifyRole(&AppItem::installedChanged, InstalledRole);
    notifyRole(&AppItem::updateAvailableChanged, UpdateAvailableRole);
}

void AppListModel::appendItem(AppItem *item)
{
    if (!item)
        return;
    beginInsertRows(QModelIndex(), m_items.size(), m_items.size());
    m_items.append(item);
    endInsertRows();
    connectItemSignals(item);
}

void AppListModel::appendItems(const QList<AppItem *> &items)
{
    QList<AppItem *> validItems;
    validItems.reserve(items.size());
    for (AppItem *item : items) {
        if (item)
            validItems.append(item);
    }
    if (validItems.isEmpty())
        return;

    // One begin/end pair for the whole batch: emitting per-row signals for a
    // 4000-row catalog floods the proxies and views on the GUI thread.
    const int first = m_items.size();
    beginInsertRows(QModelIndex(), first, first + validItems.size() - 1);
    m_items.append(validItems);
    endInsertRows();
    for (AppItem *item : validItems)
        connectItemSignals(item);
}

void AppListModel::clear()
{
    beginResetModel();
    if (m_ownsItems)
        qDeleteAll(m_items);
    m_items.clear();
    endResetModel();
}

void AppListModel::setOwnsItems(bool owns)
{
    m_ownsItems = owns;
}

bool AppListModel::ownsItems() const
{
    return m_ownsItems;
}

QList<AppItem *> AppListModel::items() const { return m_items; }

AppItem *AppListModel::itemAt(int row) const
{
    if (row < 0 || row >= m_items.size())
        return nullptr;
    return m_items.at(row);
}

int AppListModel::indexOfKey(const QString &key) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i)->key() == key)
            return i;
    }
    return -1;
}
