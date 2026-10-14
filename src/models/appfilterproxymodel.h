// SPDX-FileCopyrightText: 2026 dodo-store authors
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QSortFilterProxyModel>
#include <QStringList>

// Case-insensitive substring filter over the name/summary roles of
// AppListModel. Exposed to QML as AppFilterProxyModel; DTK's QML-level
// SortFilterModel is O(n²) on large catalogs (pure-JS DelegateModel group
// rebuilds) and unusable for the full Flathub listing.
class AppFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(bool featuredOnly READ featuredOnly WRITE setFeaturedOnly NOTIFY featuredOnlyChanged)
    Q_PROPERTY(QStringList featuredAppIds READ featuredAppIds WRITE setFeaturedAppIds NOTIFY featuredAppIdsChanged)

public:
    explicit AppFilterProxyModel(QObject *parent = nullptr);

    QString filterText() const;
    void setFilterText(const QString &text);
    bool featuredOnly() const;
    void setFeaturedOnly(bool enabled);
    QStringList featuredAppIds() const;
    void setFeaturedAppIds(const QStringList &appIds);

Q_SIGNALS:
    void filterTextChanged();
    void featuredOnlyChanged();
    void featuredAppIdsChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString m_filterText;
    bool m_featuredOnly = false;
    QStringList m_featuredAppIds;
};
