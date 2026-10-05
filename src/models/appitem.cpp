// SPDX-FileCopyrightText: 2026 dodo-store authors
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "appitem.h"

#include <QVariant>

AppItem::AppItem(const QString &appId,
                 const QString &remote,
                 const QString &branch,
                 const QString &arch,
                 bool isRuntime,
                 QObject *parent)
    : QObject(parent)
    , m_appId(appId)
    , m_remote(remote)
    , m_branch(branch)
    , m_arch(arch)
    , m_isRuntime(isRuntime)
{
}

QString AppItem::appId() const { return m_appId; }
QString AppItem::name() const { return m_name; }
void AppItem::setName(const QString &name) { if (m_name != name) { m_name = name; Q_EMIT nameChanged(); } }
QString AppItem::summary() const { return m_summary; }
void AppItem::setSummary(const QString &summary) { if (m_summary != summary) { m_summary = summary; Q_EMIT summaryChanged(); } }
QString AppItem::description() const { return m_description; }
void AppItem::setDescription(const QString &description) { if (m_description != description) { m_description = description; Q_EMIT descriptionChanged(); } }
QString AppItem::version() const { return m_version; }
void AppItem::setVersion(const QString &version) { if (m_version != version) { m_version = version; Q_EMIT versionChanged(); } }
QString AppItem::remote() const { return m_remote; }
QString AppItem::branch() const { return m_branch; }
QString AppItem::arch() const { return m_arch; }
QString AppItem::runtime() const { return m_runtime; }
void AppItem::setRuntime(const QString &runtime) { if (m_runtime != runtime) { m_runtime = runtime; Q_EMIT runtimeChanged(); } }
QString AppItem::license() const { return m_license; }
void AppItem::setLicense(const QString &license) { if (m_license != license) { m_license = license; Q_EMIT licenseChanged(); } }
QString AppItem::developer() const { return m_developer; }
void AppItem::setDeveloper(const QString &developer) { if (m_developer != developer) { m_developer = developer; Q_EMIT developerChanged(); } }
QString AppItem::homepage() const { return m_homepage; }
void AppItem::setHomepage(const QString &homepage) { if (m_homepage != homepage) { m_homepage = homepage; Q_EMIT homepageChanged(); } }
QString AppItem::category() const { return m_category; }
void AppItem::setCategory(const QString &category) { if (m_category != category) { m_category = category; Q_EMIT categoryChanged(); } }
QString AppItem::releaseNotes() const { return m_releaseNotes; }
void AppItem::setReleaseNotes(const QString &releaseNotes) { if (m_releaseNotes != releaseNotes) { m_releaseNotes = releaseNotes; Q_EMIT releaseNotesChanged(); } }
QUrl AppItem::iconUrl() const { return m_iconUrl; }
void AppItem::setIcon(const QUrl &icon) { if (m_iconUrl != icon) { m_iconUrl = icon; Q_EMIT iconChanged(); } }

QList<QUrl> AppItem::screenshots() const { return m_screenshots; }
void AppItem::setScreenshots(const QList<QUrl> &screenshots)
{
    if (m_screenshots != screenshots) { m_screenshots = screenshots; Q_EMIT screenshotsChanged(); }
}
QVariantList AppItem::screenshotsQml() const
{
    QVariantList list;
    for (const auto &u : m_screenshots)
        list.append(u);
    return list;
}
void AppItem::setScreenshotsQml(const QVariantList &screenshots)
{
    QList<QUrl> urls;
    for (const auto &v : screenshots)
        urls.append(v.toUrl());
    setScreenshots(urls);
}

quint64 AppItem::installedSize() const { return m_installedSize; }
void AppItem::setInstalledSize(quint64 size) { if (m_installedSize != size) { m_installedSize = size; Q_EMIT installedSizeChanged(); } }
quint64 AppItem::downloadSize() const { return m_downloadSize; }
void AppItem::setDownloadSize(quint64 size) { if (m_downloadSize != size) { m_downloadSize = size; Q_EMIT downloadSizeChanged(); } }
bool AppItem::installed() const { return m_installed; }
void AppItem::setInstalled(bool installed) { if (m_installed != installed) { m_installed = installed; Q_EMIT installedChanged(); } }
bool AppItem::updateAvailable() const { return m_updateAvailable; }
void AppItem::setUpdateAvailable(bool available) { if (m_updateAvailable != available) { m_updateAvailable = available; Q_EMIT updateAvailableChanged(); } }
bool AppItem::isRuntime() const { return m_isRuntime; }

QString AppItem::key() const
{
    return m_remote + QLatin1Char(':') + m_appId + QLatin1Char(':') + m_branch;
}