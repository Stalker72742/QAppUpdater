#include "UpdaterWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyleHints>
#include <QTextBrowser>
#include <QVBoxLayout>

#include "QAppUpdater/AppInfo.h"
#include "QAppUpdater/InstallLayout.h"
#include "UpdateInstaller.h"
#include "UpdaterPalette.h"

namespace QAppUpdater {

namespace {

// The app's own icon, as Explorer shows it.
QIcon appIcon()
{
    QIcon const icon = QFileIconProvider().icon(QFileInfo(QDir(InstallLayout::rootDir()).filePath(appInfo().appExe)));
    return icon.isNull() ? QApplication::windowIcon() : icon;
}

// One line, elided in the middle to whatever width it gets; the full text is
// its tooltip. For paths, whose interesting part is at both ends.
class ElidedLabel : public QLabel {
public:
    using QLabel::QLabel;

    void setFullText(QString const& text)
    {
        m_text = text;
        setToolTip(text);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setPen(palette().color(foregroundRole()));
        painter.drawText(contentsRect(), Qt::AlignLeft | Qt::AlignVCenter,
                         fontMetrics().elidedText(m_text, Qt::ElideMiddle, contentsRect().width()));
    }

    QSize minimumSizeHint() const override { return {0, fontMetrics().height()}; }
    QSize sizeHint() const override { return {fontMetrics().horizontalAdvance(m_text), fontMetrics().height()}; }

private:
    QString m_text;
};

QLabel* mutedLabel(QString const& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("Muted"));
    label->setWordWrap(true);
    return label;
}

// Label on the left, editor on the right, like a settings row in the app.
QWidget* formRow(QString const& name, QString const& description, QWidget* editor, QWidget* parent)
{
    auto* row = new QWidget(parent);
    row->setObjectName(QStringLiteral("FormRow"));
    row->setAttribute(Qt::WA_StyledBackground);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 12, 0, 12);
    layout->setSpacing(16);

    auto* text = new QVBoxLayout;
    text->setSpacing(3);
    auto* title = new QLabel(name, row);
    title->setObjectName(QStringLiteral("RowName"));
    text->addWidget(title);
    if (!description.isEmpty())
        text->addWidget(mutedLabel(description, row));
    layout->addLayout(text, 1);
    layout->addWidget(editor, 0, Qt::AlignVCenter);
    return row;
}

} // namespace

UpdaterWindow::UpdaterWindow(UpdaterConfig config, QString configPath, QWidget* parent)
    : QWidget(parent)
    , m_config(std::move(config))
    , m_configPath(std::move(configPath))
    , m_finder(new ReleaseFinder(this))
    , m_installer(new UpdateInstaller(this))
{
    setWindowTitle(tr("%1 Updater").arg(appInfo().name));
    setWindowIcon(appIcon());
    resize(600, 520);
    setMinimumSize(480, 420);

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(buildMainPage());
    m_pages->addWidget(buildSourcePage());
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_pages);

    connect(m_finder, &ReleaseFinder::found, this, &UpdaterWindow::onFound);
    connect(m_finder, &ReleaseFinder::failed, this, [this](QString const& error) {
        setBusy(false);
        m_haveRelease = false;
        m_installButton->setEnabled(false);
        setStatus(tr("Couldn't check for updates"), updaterPalette().danger);
        m_notes->setPlainText(error);
    });

    connect(m_installer, &UpdateInstaller::stageChanged, m_stageLabel, &QLabel::setText);
    connect(m_installer, &UpdateInstaller::progress, this, [this](qint64 done, qint64 total) {
        if (total <= 0) {
            m_progress->setRange(0, 0);
            return;
        }
        m_progress->setRange(0, 1000);
        m_progress->setValue(static_cast<int>(done * 1000 / total));
    });
    connect(m_installer, &UpdateInstaller::finished, this, [this](bool ok, QString const& message) {
        setBusy(false);
        m_progress->hide();
        m_stageLabel->setText(message);
        Palette const p = updaterPalette();
        if (ok) {
            m_installedLabel->setText(tr("Installed version: %1").arg(m_release.version.toString()));
            setStatus(tr("Up to date"), p.ok);
            m_installButton->setEnabled(false);
        } else {
            setStatus(tr("Update failed"), p.danger);
            m_installButton->setEnabled(m_haveRelease); // retry
        }
    });

    applyStyle();
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &UpdaterWindow::applyStyle);
}

QWidget* UpdaterWindow::buildMainPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 22, 24, 20);
    layout->setSpacing(14);

    auto* header = new QHBoxLayout;
    header->setSpacing(14);
    auto* icon = new QLabel(page);
    icon->setPixmap(appIcon().pixmap(40, 40));
    header->addWidget(icon, 0, Qt::AlignTop);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(2);
    auto* title = new QLabel(tr("%1 Updater").arg(appInfo().name), page);
    title->setObjectName(QStringLiteral("Title"));
    titles->addWidget(title);
    QVersionNumber const installed = InstallLayout::installedVersion(InstallLayout::rootDir());
    m_installedLabel = mutedLabel(tr("Installed version: %1")
                                      .arg(installed.isNull() ? tr("unknown") : installed.toString()),
                                  page);
    titles->addWidget(m_installedLabel);
    header->addLayout(titles, 1);
    layout->addLayout(header);

    auto* card = new QFrame(page);
    card->setObjectName(QStringLiteral("Card"));
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 14, 16, 14);
    cardLayout->setSpacing(10);
    auto* statusRow = new QHBoxLayout;
    statusRow->setSpacing(8);
    m_statusDot = new QLabel(card);
    m_statusDot->setFixedSize(10, 10);
    statusRow->addWidget(m_statusDot);
    m_statusLabel = new QLabel(card);
    m_statusLabel->setObjectName(QStringLiteral("Status"));
    statusRow->addWidget(m_statusLabel, 1);
    cardLayout->addLayout(statusRow);
    m_notes = new QTextBrowser(card);
    m_notes->setObjectName(QStringLiteral("Notes"));
    m_notes->setOpenExternalLinks(true);
    m_notes->setFrameShape(QFrame::NoFrame);
    cardLayout->addWidget(m_notes, 1);
    layout->addWidget(card, 1);

    auto* sourceRow = new QHBoxLayout;
    m_sourceLabel = new ElidedLabel(page);
    m_sourceLabel->setObjectName(QStringLiteral("Muted"));
    updateSourceLabel();
    sourceRow->addWidget(m_sourceLabel, 1);
    m_sourceButton = new QPushButton(tr("Change source..."), page);
    m_sourceButton->setObjectName(QStringLiteral("LinkButton"));
    m_sourceButton->setCursor(Qt::PointingHandCursor);
    connect(m_sourceButton, &QPushButton::clicked, this, &UpdaterWindow::showSourcePage);
    sourceRow->addWidget(m_sourceButton);
    layout->addLayout(sourceRow);

    m_progress = new QProgressBar(page);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(6);
    m_progress->hide();
    layout->addWidget(m_progress);
    m_stageLabel = mutedLabel(QString(), page);
    layout->addWidget(m_stageLabel);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    m_checkButton = new QPushButton(tr("Check again"), page);
    connect(m_checkButton, &QPushButton::clicked, this, &UpdaterWindow::check);
    buttons->addWidget(m_checkButton);
    m_installButton = new QPushButton(tr("Install"), page);
    m_installButton->setObjectName(QStringLiteral("Primary"));
    m_installButton->setEnabled(false);
    connect(m_installButton, &QPushButton::clicked, this, &UpdaterWindow::install);
    buttons->addWidget(m_installButton);
    m_closeButton = new QPushButton(tr("Close"), page);
    connect(m_closeButton, &QPushButton::clicked, this, [this] {
        if (m_installer->isRunning())
            m_installer->cancel();
        else
            close();
    });
    buttons->addWidget(m_closeButton);
    layout->addLayout(buttons);
    return page;
}

QWidget* UpdaterWindow::buildSourcePage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 22, 24, 20);
    layout->setSpacing(0);

    auto* title = new QLabel(tr("Update source"), page);
    title->setObjectName(QStringLiteral("Title"));
    layout->addWidget(title);
    auto* subtitle = mutedLabel(tr("Saved to %1").arg(QDir::toNativeSeparators(m_configPath)), page);
    layout->addWidget(subtitle);
    layout->addSpacing(12);

    m_sourceCombo = new QComboBox(page);
    m_sourceCombo->addItem(tr("GitHub releases"));
    m_sourceCombo->addItem(tr("Local build"));
    m_sourceCombo->setMinimumWidth(180);
    connect(m_sourceCombo, &QComboBox::currentIndexChanged, this, &UpdaterWindow::updateSourceRows);
    layout->addWidget(formRow(tr("Source"), tr("Where new versions come from."), m_sourceCombo, page));

    m_repoEdit = new QLineEdit(page);
    m_repoEdit->setPlaceholderText(QStringLiteral("owner/name"));
    m_repoEdit->setMinimumWidth(220);
    m_repoRow = formRow(tr("Repository"), tr("Its latest release needs a %1-<version>-win64.zip asset.").arg(appInfo().name),
                        m_repoEdit, page);
    layout->addWidget(m_repoRow);

    m_prereleaseSwitch = new QCheckBox(page);
    m_prereleaseRow = formRow(tr("Include pre-releases"), QString(), m_prereleaseSwitch, page);
    layout->addWidget(m_prereleaseRow);

    auto* pathEditor = new QWidget(page);
    auto* pathLayout = new QHBoxLayout(pathEditor);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(6);
    m_pathEdit = new QLineEdit(pathEditor);
    m_pathEdit->setMinimumWidth(200);
    pathLayout->addWidget(m_pathEdit);
    auto* folderButton = new QPushButton(tr("Folder..."), pathEditor);
    connect(folderButton, &QPushButton::clicked, this, [this] {
        QString const dir = QFileDialog::getExistingDirectory(this, tr("Choose a Deployed Build"), m_pathEdit->text());
        if (!dir.isEmpty())
            m_pathEdit->setText(QDir::toNativeSeparators(dir));
    });
    pathLayout->addWidget(folderButton);
    auto* zipButton = new QPushButton(tr("Zip..."), pathEditor);
    connect(zipButton, &QPushButton::clicked, this, [this] {
        QString const file = QFileDialog::getOpenFileName(this, tr("Choose a Package"), m_pathEdit->text(),
                                                          tr("%1 package (%1-*.zip)").arg(appInfo().name));
        if (!file.isEmpty())
            m_pathEdit->setText(QDir::toNativeSeparators(file));
    });
    pathLayout->addWidget(zipButton);
    m_pathRow = formRow(tr("Build"), tr("A deployed folder (with package.json), or its package zip."), pathEditor, page);
    layout->addWidget(m_pathRow);

    m_restartSwitch = new QCheckBox(page);
    layout->addWidget(formRow(tr("Restart %1").arg(appInfo().name), tr("Start it again after installing."), m_restartSwitch, page));

    m_sourceError = new QLabel(page);
    m_sourceError->setObjectName(QStringLiteral("Error"));
    m_sourceError->setWordWrap(true);
    layout->addWidget(m_sourceError);
    layout->addStretch(1);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto* cancel = new QPushButton(tr("Cancel"), page);
    connect(cancel, &QPushButton::clicked, this, [this] { m_pages->setCurrentIndex(0); });
    buttons->addWidget(cancel);
    auto* save = new QPushButton(tr("Save"), page);
    save->setObjectName(QStringLiteral("Primary"));
    connect(save, &QPushButton::clicked, this, &UpdaterWindow::saveSourcePage);
    buttons->addWidget(save);
    layout->addLayout(buttons);
    return page;
}

void UpdaterWindow::showSourcePage()
{
    m_sourceCombo->setCurrentIndex(m_config.source == UpdaterConfig::Source::Local ? 1 : 0);
    m_repoEdit->setText(m_config.repo);
    m_prereleaseSwitch->setChecked(m_config.prerelease);
    m_pathEdit->setText(QDir::toNativeSeparators(m_config.localPath));
    m_restartSwitch->setChecked(m_config.restartApp);
    m_sourceError->clear();
    updateSourceRows();
    m_pages->setCurrentIndex(1);
}

void UpdaterWindow::updateSourceRows()
{
    bool const local = m_sourceCombo->currentIndex() == 1;
    m_repoRow->setVisible(!local);
    m_prereleaseRow->setVisible(!local);
    m_pathRow->setVisible(local);
}

void UpdaterWindow::saveSourcePage()
{
    UpdaterConfig config = m_config;
    config.source = m_sourceCombo->currentIndex() == 1 ? UpdaterConfig::Source::Local : UpdaterConfig::Source::GitHub;
    config.repo = m_repoEdit->text().trimmed();
    config.prerelease = m_prereleaseSwitch->isChecked();
    config.localPath = QDir::fromNativeSeparators(m_pathEdit->text().trimmed());
    config.restartApp = m_restartSwitch->isChecked();

    QString error;
    if (!config.save(m_configPath, &error)) {
        m_sourceError->setText(tr("Couldn't save %1: %2").arg(QDir::toNativeSeparators(m_configPath), error));
        return;
    }
    m_config = config;
    updateSourceLabel();
    m_pages->setCurrentIndex(0);
    check();
}

void UpdaterWindow::updateSourceLabel()
{
    static_cast<ElidedLabel*>(m_sourceLabel)->setFullText(m_config.describeSource());
}

void UpdaterWindow::check()
{
    if (m_installer->isRunning())
        return;
    m_haveRelease = false;
    m_installButton->setEnabled(false);
    m_notes->clear();
    m_stageLabel->clear();
    setStatus(tr("Checking for updates..."), updaterPalette().textMuted);
    setBusy(true);
    m_finder->find(m_config);
}

void UpdaterWindow::onFound(ReleaseInfo const& release)
{
    setBusy(false);
    m_release = release;
    m_haveRelease = true;

    Palette const p = updaterPalette();
    QVersionNumber const installed = InstallLayout::installedVersion(InstallLayout::rootDir());
    bool const newer = InstallLayout::isNewer(release.version, installed);
    if (newer) {
        setStatus(tr("Version %1 is available").arg(release.version.toString()), p.accent);
        m_installButton->setText(tr("Install %1").arg(release.version.toString()));
    } else {
        setStatus(tr("You're up to date (latest: %1)").arg(release.version.toString()), p.ok);
        m_installButton->setText(tr("Reinstall"));
    }
    m_installButton->setEnabled(true);
    m_notes->setMarkdown(release.notes.isEmpty() ? tr("*No release notes.*") : release.notes);

    if (newer && m_autoInstall) {
        m_autoInstall = false;
        install();
    }
}

void UpdaterWindow::install()
{
    if (!m_haveRelease)
        return;
    setBusy(true);
    m_progress->show();
    m_progress->setRange(0, 0);
    m_installer->install(m_release, m_config.restartApp);
    // It may already have failed (and re-enabled everything) synchronously.
    if (m_installer->isRunning())
        m_closeButton->setText(tr("Cancel"));
}

void UpdaterWindow::setBusy(bool busy)
{
    m_checkButton->setEnabled(!busy);
    m_sourceButton->setEnabled(!busy);
    if (busy)
        m_installButton->setEnabled(false);
    else
        m_closeButton->setText(tr("Close"));
}

void UpdaterWindow::setStatus(QString const& text, QColor const& dot)
{
    m_statusLabel->setText(text);
    m_statusDot->setStyleSheet(QStringLiteral("background: %1; border-radius: 5px;").arg(dot.name()));
}

void UpdaterWindow::closeEvent(QCloseEvent* event)
{
    // Cancel what can be canceled; once files are being replaced, cancel() is
    // a no-op and the window stays until the (short) copy is done.
    if (m_installer->isRunning()) {
        m_installer->cancel();
        if (m_installer->isRunning()) {
            event->ignore();
            return;
        }
    }
    m_finder->cancel();
    QWidget::closeEvent(event);
}

void UpdaterWindow::applyStyle()
{
    Palette const p = updaterPalette();
    applyApplicationPalette(p);
    QString sheet = QStringLiteral(R"(
        QWidget { color: @text; font-size: 12px; }
        QPushButton { background: @bg2; color: @text; border: 1px solid @border; border-radius: 6px; padding: 6px 14px; }
        QPushButton:hover { background: @bg3; }
        QPushButton:disabled { color: @textFaint; }
        QLineEdit, QComboBox { background: @bg2; color: @text; border: 1px solid @border; border-radius: 6px; padding: 5px 8px; }
        QLineEdit:focus, QComboBox:focus { border-color: @accent; }
        QComboBox QAbstractItemView { background: @bg2; color: @text; selection-background-color: @bg3; }
        QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid @border; border-radius: 4px; background: @bg2; }
        QCheckBox::indicator:checked { background: @accent; border-color: @accent; }
        UpdaterWindow, QStackedWidget { background: @bg1; }
        QLabel#Title { color: @text; font-size: 18px; font-weight: 600; }
        QLabel#Muted { color: @textMuted; font-size: 11px; }
        QLabel#Status { color: @text; font-size: 13px; font-weight: 600; }
        QLabel#RowName { color: @text; font-size: 13px; }
        QLabel#Error { color: @danger; padding-top: 8px; }
        QWidget#FormRow { border-top: 1px solid @border; }
        QFrame#Card { background: @bg0; border: 1px solid @border; border-radius: 8px; }
        QTextBrowser#Notes { background: transparent; color: @text; border: none; }
        QPushButton#Primary { background: @accent; color: @accentContrast; border-color: @accent; }
        QPushButton#Primary:hover { background: @accent; border-color: @text; }
        QPushButton#Primary:disabled { background: @bg2; color: @textFaint; border-color: @border; }
        QPushButton#LinkButton { background: transparent; border: none; color: @accent; padding: 2px 4px; }
        QPushButton#LinkButton:hover { text-decoration: underline; }
        QProgressBar { background: @bg3; border: none; border-radius: 3px; }
        QProgressBar::chunk { background: @accent; border-radius: 3px; }
    )");
    QList<std::pair<QString, QString>> const tokens = {
        {QStringLiteral("@accentContrast"), p.accentContrast.name()},
        {QStringLiteral("@textMuted"), p.textMuted.name()},
        {QStringLiteral("@textFaint"), p.textFaint.name()},
        {QStringLiteral("@accent"), p.accent.name()},
        {QStringLiteral("@border"), p.border.name()},
        {QStringLiteral("@danger"), p.danger.name()},
        {QStringLiteral("@text"), p.text.name()},
        {QStringLiteral("@bg0"), p.bg0.name()},
        {QStringLiteral("@bg1"), p.bg1.name()},
        {QStringLiteral("@bg2"), p.bg2.name()},
        {QStringLiteral("@bg3"), p.bg3.name()},
    };
    for (auto const& [token, value] : tokens)
        sheet.replace(token, value);
    setStyleSheet(sheet);
}

} // namespace QAppUpdater
