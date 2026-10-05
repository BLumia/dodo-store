// SPDX-FileCopyrightText: 2026 dodo-store authors
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QList>

// A single application (flatpak ref) surfaced to QML. Instances are owned by a
// model and are populated from a mix of FlatpakRef data and AppStream metadata.
class AppItem : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString appId READ appId CONSTANT)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QString summary READ summary WRITE setSummary NOTIFY summaryChanged)
    Q_PROPERTY(QString description READ description WRITE setDescription NOTIFY descriptionChanged)
    Q_PROPERTY(QString version READ version WRITE setVersion NOTIFY versionChanged)
    Q_PROPERTY(QString remote READ remote CONSTANT)
    Q_PROPERTY(QString branch READ branch CONSTANT)
    Q_PROPERTY(QString arch READ arch CONSTANT)
    Q_PROPERTY(QString runtime READ runtime WRITE setRuntime NOTIFY runtimeChanged)
    Q_PROPERTY(QString license READ license WRITE setLicense NOTIFY licenseChanged)
    Q_PROPERTY(QString developer READ developer WRITE setDeveloper NOTIFY developerChanged)
    Q_PROPERTY(QString homepage READ homepage WRITE setHomepage NOTIFY homepageChanged)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY categoryChanged)
    Q_PROPERTY(QString releaseNotes READ releaseNotes WRITE setReleaseNotes NOTIFY releaseNotesChanged)
    Q_PROPERTY(QUrl icon READ iconUrl WRITE setIcon NOTIFY iconChanged)
    Q_PROPERTY(QVariantList screenshots READ screenshotsQml WRITE setScreenshotsQml NOTIFY screenshotsChanged)
    Q_PROPERTY(quint64 installedSize READ installedSize WRITE setInstalledSize NOTIFY installedSizeChanged)
    Q_PROPERTY(quint64 downloadSize READ downloadSize WRITE setDownloadSize NOTIFY downloadSizeChanged)
    Q_PROPERTY(bool installed READ installed WRITE setInstalled NOTIFY installedChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable WRITE setUpdateAvailable NOTIFY updateAvailableChanged)
    Q_PROPERTY(bool isRuntime READ isRuntime CONSTANT)

public:
    explicit AppItem(const QString &appId,
                     const QString &remote,
                     const QString &branch,
                     const QString &arch,
                     bool isRuntime,
                     QObject *parent = nullptr);

    QString appId() const;
    QString name() const;
    void setName(const QString &name);
    QString summary() const;
    void setSummary(const QString &summary);
    QString description() const;
    void setDescription(const QString &description);
    QString version() const;
    void setVersion(const QString &version);
    QString remote() const;
    QString branch() const;
    QString arch() const;
    QString runtime() const;
    void setRuntime(const QString &runtime);
    QString license() const;
    void setLicense(const QString &license);
    QString developer() const;
    void setDeveloper(const QString &developer);
    QString homepage() const;
    void setHomepage(const QString &homepage);
    QString category() const;
    void setCategory(const QString &category);
    QString releaseNotes() const;
    void setReleaseNotes(const QString &releaseNotes);
    QUrl iconUrl() const;
    void setIcon(const QUrl &icon);
    QList<QUrl> screenshots() const;
    void setScreenshots(const QList<QUrl> &screenshots);
    QVariantList screenshotsQml() const;
    void setScreenshotsQml(const QVariantList &screenshots);
    quint64 installedSize() const;
    void setInstalledSize(quint64 size);
    quint64 downloadSize() const;
    void setDownloadSize(quint64 size);
    bool installed() const;
    void setInstalled(bool installed);
    bool updateAvailable() const;
    void setUpdateAvailable(bool available);
    bool isRuntime() const;

    // Unique key used to correlate items across remote/installed/update views.
    QString key() const;

Q_SIGNALS:
    void nameChanged();
    void summaryChanged();
    void descriptionChanged();
    void versionChanged();
    void runtimeChanged();
    void licenseChanged();
    void developerChanged();
    void homepageChanged();
    void categoryChanged();
    void releaseNotesChanged();
    void iconChanged();
    void screenshotsChanged();
    void installedSizeChanged();
    void downloadSizeChanged();
    void installedChanged();
    void updateAvailableChanged();

private:
    QString m_appId;
    QString m_name;
    QString m_summary;
    QString m_description;
    QString m_version;
    QString m_remote;
    QString m_branch;
    QString m_arch;
    QString m_runtime;
    QString m_license;
    QString m_developer;
    QString m_homepage;
    QString m_category;
    QString m_releaseNotes;
    QUrl m_iconUrl;
    QList<QUrl> m_screenshots;
    quint64 m_installedSize = 0;
    quint64 m_downloadSize = 0;
    bool m_installed = false;
    bool m_updateAvailable = false;
    bool m_isRuntime = false;
};