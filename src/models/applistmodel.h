// SPDX-FileCopyrightText: 2026 dodo-store authors
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QAbstractListModel>
#include <QList>

class AppItem;

// Owns a list of AppItem and exposes them to QML through standard list roles.
// Mutations (append/clear/etc.) happen on the GUI thread and trigger proper
// begin/endInsertRows notifications.
class AppListModel : public QAbstractListModel
{
    Q_OBJECT
    // rowCount() is a Q_INVOKABLE method, not a bindable property; expose a
    // plain count for QML (e.g. "N applications installed").
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Roles {
        AppIdRole = Qt::UserRole + 1,
        NameRole,
        SummaryRole,
        DescriptionRole,
        VersionRole,
        RemoteRole,
        BranchRole,
        ArchRole,
        RuntimeRole,
        LicenseRole,
        IconRole,
        ScreenshotsRole,
        InstalledSizeRole,
        DownloadSizeRole,
        InstalledRole,
        UpdateAvailableRole,
        IsRuntimeRole,
        ItemRole
    };

    explicit AppListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int count() const;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void appendItem(AppItem *item);
    void appendItems(const QList<AppItem *> &items);
    void clear();
    QList<AppItem *> items() const;
    AppItem *itemAt(int row) const;
    int indexOfKey(const QString &key) const;

Q_SIGNALS:
    void countChanged();

private:
    QList<AppItem *> m_items;
};