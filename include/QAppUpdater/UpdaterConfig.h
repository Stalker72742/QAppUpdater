#pragma once

#include <QString>

namespace QAppUpdater {

// The updater's settings: <install root>/updater.json, written by the updater window's "Update source" page
// or by hand (`Updater --init-config` writes a template, `Updater --help` documents every field).
// Command-line options override it for one run without saving.
struct UpdaterConfig {
    enum class Source {
        GitHub, // latest release of `repo` on GitHub
        Local,  // a deployed folder or package zip on disk (`localPath`)
    };

    Source source{Source::GitHub};
    QString repo;           // "owner/name"
    bool prerelease{false}; // GitHub: also consider pre-releases
    QString localPath;      // Local: a deployed folder (with package.json), or a package zip
    bool restartApp{true};  // start the app after installing

    static UpdaterConfig defaults();

    // Missing file -> defaults. Returns false (and fills `error`) if the file exists but isn't valid;
    // `config` then holds the defaults.
    static bool load(QString const& path, UpdaterConfig& config, QString* error = nullptr);
    bool save(QString const& path, QString* error = nullptr) const;

    static QString defaultPath();
    static QString sourceName(Source source); // "github" / "local"
    // "GitHub: owner/name" / "Local: D:/builds/dist", for status lines.
    QString describeSource() const;
};

} // namespace QAppUpdater
