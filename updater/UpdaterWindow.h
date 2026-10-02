#pragma once

#include <QWidget>

#include "QAppUpdater/ReleaseFinder.h"
#include "QAppUpdater/UpdaterConfig.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTextBrowser;

namespace QAppUpdater {

class UpdateInstaller;

// The updater's GUI: installed vs. latest version, release notes, install
// progress, and an "update source" page that edits updater.json.
class UpdaterWindow : public QWidget {
    Q_OBJECT

public:
    UpdaterWindow(UpdaterConfig config, QString configPath, QWidget* parent = nullptr);

    void check();

    // Starts installing as soon as a newer release is found.
    void setAutoInstall(bool autoInstall) { m_autoInstall = autoInstall; }

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QWidget* buildMainPage();
    QWidget* buildSourcePage();
    void showSourcePage();
    void saveSourcePage();
    void updateSourceRows();
    void updateSourceLabel();

    void setStatus(QString const& text, QColor const& dot);
    void setBusy(bool busy);
    void install();
    void onFound(ReleaseInfo const& release);
    void applyStyle();

    UpdaterConfig m_config;
    QString m_configPath;
    ReleaseFinder* m_finder{nullptr};
    UpdateInstaller* m_installer{nullptr};
    ReleaseInfo m_release;
    bool m_haveRelease{false};
    bool m_autoInstall{false};

    QStackedWidget* m_pages{nullptr};

    QLabel* m_installedLabel{nullptr};
    QLabel* m_statusDot{nullptr};
    QLabel* m_statusLabel{nullptr};
    QTextBrowser* m_notes{nullptr};
    QLabel* m_sourceLabel{nullptr};
    QLabel* m_stageLabel{nullptr};
    QProgressBar* m_progress{nullptr};
    QPushButton* m_checkButton{nullptr};
    QPushButton* m_installButton{nullptr};
    QPushButton* m_closeButton{nullptr};
    QPushButton* m_sourceButton{nullptr};

    QComboBox* m_sourceCombo{nullptr};
    QWidget* m_repoRow{nullptr};
    QLineEdit* m_repoEdit{nullptr};
    QWidget* m_prereleaseRow{nullptr};
    QCheckBox* m_prereleaseSwitch{nullptr};
    QWidget* m_pathRow{nullptr};
    QLineEdit* m_pathEdit{nullptr};
    QCheckBox* m_restartSwitch{nullptr};
    QLabel* m_sourceError{nullptr};
};

} // namespace QAppUpdater
