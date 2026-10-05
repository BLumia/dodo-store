// SPDX-FileCopyrightText: 2026 dodo-store authors
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "appfilterproxymodel.h"
#include "applistmodel.h"

AppFilterProxyModel::AppFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setFilterCaseSensitivity(Qt::CaseInsensitive);
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

bool AppFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (filterRegularExpression().pattern().isEmpty())
        return true;

    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    const QString name = idx.data(AppListModel::NameRole).toString();
    const QString summary = idx.data(AppListModel::SummaryRole).toString();
    return name.contains(filterRegularExpression())
           || summary.contains(filterRegularExpression());
}
