// The updater: checks for and installs new versions of the app it was built for. Lives next to the app exe
// and shares its runtime folder (bin/ by default). With a command it runs as a CLI; without one it opens its
// window (--install there starts installing as soon as a newer release is found). The CLI with --report
// streams its progress to the running app, which then shows the install instead of a second window.

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTextStream>

#include <cstring>
#include <functional>
#include <memory>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "Logging.h"
#include "QAppUpdater/AppInfo.h"
#include "QAppUpdater/InstallLayout.h"
#include "QAppUpdater/ReleaseFinder.h"
#include "QAppUpdater/UpdaterConfig.h"
#include "UpdateInstaller.h"
#include "UpdaterWindow.h"

using namespace QAppUpdater;

namespace {

enum ExitCode {
    kOk = 0,
    kError = 1,
    kUpdateAvailable = 2, // --check only
    kUsage = 64,
};

char const* const kCommands[] = {"--help", "-h", "--version", "--check", "--update", "--init-config", "--print-config"};

char const* const kConfigHelp = R"(updater.json (next to Updater.exe; every field is optional):
  {
    "source": "github",            "github" = GitHub releases, "local" = a build on disk
    "github": {
      "repo": "owner/name",        releases need a <App>-<version>-win64.zip asset
      "prerelease": false          also consider pre-releases
    },
    "local": {
      "path": "D:/builds/dist"     a deployed folder (with package.json), or its package zip
    },
    "restartApp": true             start the app again after installing
  }

Exit codes: 0 = done / up to date, 1 = error, 2 = an update is available (--check), 64 = bad arguments.
)";

QTextStream& out()
{
    static QTextStream stream(stdout);
    return stream;
}

QTextStream& err()
{
    static QTextStream stream(stderr);
    return stream;
}

bool hasArgument(int argc, char** argv, char const* name)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], name) == 0)
            return true;
    return false;
}

bool isCliInvocation(int argc, char** argv)
{
    for (char const* command : kCommands)
        if (hasArgument(argc, argv, command))
            return true;
    return false;
}

// Updater.exe is a console program so its CLI can print; double-clicked from
// Explorer it gets a console of its own, which the GUI mode drops. A console
// shared with a terminal (launched from cmd/PowerShell) stays.
void detachOwnConsole()
{
#ifdef Q_OS_WIN
    DWORD processes[2];
    if (GetConsoleProcessList(processes, 2) == 1)
        FreeConsole();
#endif
}

// Command-line overrides of updater.json, for this run (or saved with --save).
bool applyOverrides(QCommandLineParser const& parser, UpdaterConfig& config)
{
    if (parser.isSet(QStringLiteral("source"))) {
        QString const source = parser.value(QStringLiteral("source"));
        if (source == QLatin1String("github"))
            config.source = UpdaterConfig::Source::GitHub;
        else if (source == QLatin1String("local"))
            config.source = UpdaterConfig::Source::Local;
        else {
            err() << "Unknown --source \"" << source << "\" (expected github or local).\n";
            return false;
        }
    }
    if (parser.isSet(QStringLiteral("repo")))
        config.repo = parser.value(QStringLiteral("repo"));
    if (parser.isSet(QStringLiteral("path"))) {
        config.localPath = QDir::fromNativeSeparators(QDir::current().absoluteFilePath(parser.value(QStringLiteral("path"))));
        if (!parser.isSet(QStringLiteral("source")))
            config.source = UpdaterConfig::Source::Local; // --path alone implies it
    }
    if (parser.isSet(QStringLiteral("prerelease")))
        config.prerelease = true;
    if (parser.isSet(QStringLiteral("no-restart")))
        config.restartApp = false;
    return true;
}

// --report: sends stages, progress and the outcome to the running app over its control server
// (InstallLayout::controlServerName). Without a running app it stays quiet.
class AppReporter {
public:
    AppReporter()
        : m_socket(std::make_unique<QLocalSocket>())
    {
        m_socket->connectToServer(InstallLayout::controlServerName(InstallLayout::rootDir()));
        m_socket->waitForConnected(2000);
    }

    ~AppReporter()
    {
        // The last lines must arrive before the process exits.
        if (m_socket->state() == QLocalSocket::ConnectedState) {
            m_socket->flush();
            m_socket->waitForBytesWritten(2000);
            m_socket->disconnectFromServer();
        }
    }

    void status(QString const& stage, qint64 done = 0, qint64 total = 0)
    {
        m_stage = stage;
        send("status", QJsonObject{{QStringLiteral("stage"), stage}, {QStringLiteral("done"), done}, {QStringLiteral("total"), total}});
    }

    void progress(qint64 done, qint64 total) { status(m_stage, done, total); }

    void result(bool ok, QString const& message)
    {
        send("result", QJsonObject{{QStringLiteral("ok"), ok}, {QStringLiteral("message"), message}});
    }

private:
    void send(QByteArray const& keyword, QJsonObject const& payload)
    {
        // The app closes during an install; what is sent after that goes nowhere, on purpose.
        if (m_socket->state() != QLocalSocket::ConnectedState)
            return;
        m_socket->write(keyword + ' ' + QJsonDocument(payload).toJson(QJsonDocument::Compact) + '\n');
        m_socket->flush();
    }

    std::unique_ptr<QLocalSocket> m_socket;
    QString m_stage;
};

// Runs the event loop until `done` is called; returns its exit code.
int runUntilDone(std::function<void(std::function<void(int)> done)> start)
{
    int code = kError;
    start([&code](int result) {
        code = result;
        QCoreApplication::quit();
    });
    QCoreApplication::exec();
    return code;
}

int runCli(QCommandLineParser& parser, UpdaterConfig config, QString const& configPath)
{
    if (parser.isSet(QStringLiteral("help"))) {
        out() << parser.helpText() << "\n" << kConfigHelp;
        return kOk;
    }
    if (parser.isSet(QStringLiteral("version"))) {
        out() << appInfo().name << " Updater " << appInfo().version << " (protocol " << kUpdaterProtocol << ")\n";
        return kOk;
    }

    if (parser.isSet(QStringLiteral("init-config"))) {
        if (QFile::exists(configPath) && !parser.isSet(QStringLiteral("force"))) {
            err() << QDir::toNativeSeparators(configPath) << " already exists (add --force to overwrite).\n";
            return kError;
        }
        QString error;
        if (!config.save(configPath, &error)) {
            err() << "Can't write " << QDir::toNativeSeparators(configPath) << ": " << error << "\n";
            return kError;
        }
        out() << "Wrote " << QDir::toNativeSeparators(configPath) << "\n";
        return kOk;
    }

    if (parser.isSet(QStringLiteral("save"))) {
        QString error;
        if (!config.save(configPath, &error)) {
            err() << "Can't write " << QDir::toNativeSeparators(configPath) << ": " << error << "\n";
            return kError;
        }
    }

    if (parser.isSet(QStringLiteral("print-config"))) {
        out() << "Config file: " << QDir::toNativeSeparators(configPath)
              << (QFile::exists(configPath) ? "" : " (not created yet, using defaults)") << "\n"
              << "Source:      " << config.describeSource() << "\n"
              << "Restart app: " << (config.restartApp ? "yes" : "no") << "\n";
        return kOk;
    }

    bool const update = parser.isSet(QStringLiteral("update"));
    bool const force = parser.isSet(QStringLiteral("force"));
    QVersionNumber const installed = InstallLayout::installedVersion(InstallLayout::rootDir());
    out() << "Installed: " << (installed.isNull() ? QStringLiteral("unknown") : installed.toString()) << "\n"
          << "Source:    " << config.describeSource() << "\n";
    out().flush();

    std::shared_ptr<AppReporter> reporter =
        parser.isSet(QStringLiteral("report")) ? std::make_shared<AppReporter>() : nullptr;
    if (reporter)
        reporter->status(QStringLiteral("Looking for the update..."));

    int const code = runUntilDone([&](std::function<void(int)> done) {
        auto* finder = new ReleaseFinder(qApp);
        auto* installer = new UpdateInstaller(qApp);

        QObject::connect(finder, &ReleaseFinder::failed, qApp, [done, reporter](QString const& error) {
            err() << "Error: " << error << "\n";
            if (reporter)
                reporter->result(false, error);
            done(kError);
        });
        QObject::connect(finder, &ReleaseFinder::found, qApp, [=](ReleaseInfo const& release) {
            bool const newer = InstallLayout::isNewer(release.version, installed);
            out() << "Latest:    " << release.version.toString() << (newer ? "  (update available)" : "  (up to date)")
                  << "\n";
            out().flush();
            if (!update) {
                done(newer ? kUpdateAvailable : kOk);
                return;
            }
            if (!newer && !force) {
                out() << "Nothing to do (add --force to reinstall).\n";
                if (reporter)
                    reporter->result(true, QStringLiteral("%1 is up to date.").arg(appInfo().name));
                done(kOk);
                return;
            }
            installer->install(release, config.restartApp);
        });

        QObject::connect(installer, &UpdateInstaller::stageChanged, qApp, [reporter](QString const& stage) {
            out() << stage << "\n";
            out().flush();
            if (reporter)
                reporter->status(stage);
        });
        auto lastStep = std::make_shared<int>(-1);
        QObject::connect(installer, &UpdateInstaller::progress, qApp, [lastStep, reporter](qint64 bytes, qint64 total) {
            if (reporter)
                reporter->progress(bytes, total);
            if (total <= 0)
                return;
            // One line per 10%.
            int const step = static_cast<int>(bytes * 10 / total);
            if (step == *lastStep)
                return;
            *lastStep = step;
            out() << "  " << step * 10 << "%\n";
            out().flush();
        });
        QObject::connect(installer, &UpdateInstaller::finished, qApp, [done, reporter](bool ok, QString const& message) {
            (ok ? out() : err()) << message << "\n";
            // Reaches the app only if it is still running, i.e. the install stopped before closing it.
            if (reporter)
                reporter->result(ok, message);
            done(ok ? kOk : kError);
        });

        finder->find(config);
    });
    return code;
}

} // namespace

int main(int argc, char* argv[])
{
    bool const cli = isCliInvocation(argc, argv);
    if (!cli)
        detachOwnConsole();

    // The CLI never shows a window, so it doesn't need the GUI platform plugin.
    std::unique_ptr<QCoreApplication> app =
        cli ? std::make_unique<QCoreApplication>(argc, argv) : std::make_unique<QApplication>(argc, argv);
    QCoreApplication::setApplicationName(appInfo().name + QStringLiteral(" Updater"));
    QCoreApplication::setApplicationVersion(appInfo().version);

    // The CLI's stdout/stderr are for the user; the log file keeps everything.
    if (cli && !hasArgument(argc, argv, "--verbose"))
        Logging::setMirrorToStderr(false);
    Logging::install(QStringLiteral("updater"));
    InstallLayout::removeLeftovers(InstallLayout::rootDir());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Checks for and installs %1 updates. Without a command it opens the updater window;\n"
        "the source options then apply to that window's session.").arg(appInfo().name));
    parser.addOptions({
        {{QStringLiteral("h"), QStringLiteral("help")}, QStringLiteral("Show this help, including the updater.json format.")},
        {QStringLiteral("version"), QStringLiteral("Print the updater's version.")},
        {QStringLiteral("check"), QStringLiteral("Print the installed and latest version. Exit code 2 = update available.")},
        {QStringLiteral("update"), QStringLiteral("Install the latest version if it's newer (closes the running app).")},
        {QStringLiteral("install"), QStringLiteral("Window mode: install as soon as a newer version is found.")},
        {QStringLiteral("force"), QStringLiteral("With --update: reinstall even if it isn't newer. With --init-config: overwrite.")},
        {QStringLiteral("init-config"), QStringLiteral("Write updater.json with the defaults and any source options given.")},
        {QStringLiteral("print-config"), QStringLiteral("Print the effective settings.")},
        {QStringLiteral("config"), QStringLiteral("Settings file to use instead of updater.json next to the exe."), QStringLiteral("file")},
        {QStringLiteral("source"), QStringLiteral("Update source for this run: github or local."), QStringLiteral("source")},
        {QStringLiteral("repo"), QStringLiteral("GitHub repository (owner/name) for this run."), QStringLiteral("repo")},
        {QStringLiteral("path"), QStringLiteral("Local build folder or package zip for this run (implies --source local)."), QStringLiteral("path")},
        {QStringLiteral("prerelease"), QStringLiteral("Also consider GitHub pre-releases.")},
        {QStringLiteral("no-restart"), QStringLiteral("Don't start the app after installing.")},
        {QStringLiteral("report"), QStringLiteral("With --update: send progress to the running app, which shows it.")},
        {QStringLiteral("save"), QStringLiteral("Also save the source options given to the settings file.")},
        {QStringLiteral("verbose"), QStringLiteral("Print the log to stderr as well.")},
    });
    if (!parser.parse(QCoreApplication::arguments())) {
        err() << parser.errorText() << "\nRun with --help for usage.\n";
        return kUsage;
    }
    if (!parser.positionalArguments().isEmpty()) {
        err() << "Unexpected argument: " << parser.positionalArguments().first() << "\nRun with --help for usage.\n";
        return kUsage;
    }

    QString const configPath = parser.isSet(QStringLiteral("config"))
                                   ? QDir::current().absoluteFilePath(parser.value(QStringLiteral("config")))
                                   : UpdaterConfig::defaultPath();
    UpdaterConfig config;
    QString configError;
    bool const configOk = UpdaterConfig::load(configPath, config, &configError);
    if (!configOk)
        qWarning().noquote() << "Updater: bad" << configPath << "-" << configError;
    if (!applyOverrides(parser, config))
        return kUsage;

    if (cli) {
        if (!configOk && !parser.isSet(QStringLiteral("init-config")) && !parser.isSet(QStringLiteral("help"))) {
            err() << "Can't read " << QDir::toNativeSeparators(configPath) << ": " << configError << "\n";
            return kError;
        }
        return runCli(parser, config, configPath);
    }

    QApplication::setStyle(QStringLiteral("Fusion"));
    UpdaterWindow window(config, configPath);
    window.setAutoInstall(parser.isSet(QStringLiteral("install")));
    window.show();
    window.check();
    return QApplication::exec();
}
