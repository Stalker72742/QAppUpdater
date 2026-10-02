#pragma once

#include <QObject>
#include <QPointer>
#include <QVersionNumber>

#include "UpdaterConfig.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace QAppUpdater {

// The newest release the configured source offers.
struct ReleaseInfo {
    QVersionNumber version;
    QString title;            // release name or tag
    QString notes;            // Markdown (GitHub release body); may be empty
    QString packageUrl;       // GitHub: the asset's download URL
    QString packagePath;      // Local: a package zip, or a deployed folder
    qint64 packageSize{-1};
    QByteArray packageSha256; // hex; GitHub reports it per asset, checked after download
    QString pageUrl;          // GitHub: the release's web page, for "download it yourself" messages

    bool isLocalFolder() const;
};

// Finds the latest release: GitHub's releases API, or package.json of a local folder / package zip.
// One lookup at a time; the result always arrives asynchronously as found() or failed().
class ReleaseFinder : public QObject {
    Q_OBJECT

public:
    explicit ReleaseFinder(QObject* parent = nullptr);

    void find(UpdaterConfig const& config);
    void cancel();

signals:
    void found(QAppUpdater::ReleaseInfo const& release);
    void failed(QString const& error);

private:
    void findOnGitHub(UpdaterConfig const& config);
    void findLocal(UpdaterConfig const& config);
    void onGitHubReply(QNetworkReply* reply, bool listRequest);

    QNetworkAccessManager* m_network{nullptr};
    QPointer<QNetworkReply> m_reply;
};

// Reads one file out of a zip with Windows' built-in tar.exe (bsdtar).
bool readFileFromZip(QString const& zipPath, QString const& fileInZip, QByteArray& contents, QString* error);

} // namespace QAppUpdater
