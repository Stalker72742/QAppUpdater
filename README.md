# QAppUpdater

Self-updating Qt 6 apps on Windows, wired in with one CMake call:

```cmake
qau_setup(MyApp
        NAME MyApp
        GITHUB_REPO me/MyApp
        ICON resources/app.ico
        PROTECTED_PATHS Saved)
```

That gives you:

- **`Updater.exe`** next to your exe: a window and a CLI that check GitHub releases (or a local build), download,
  verify, close your running app, replace its files and start it again. A failed update rolls back completely.
- **A tidy install folder.** Only `MyApp.exe`, `Updater.exe` and a `bin/` folder with every DLL, Qt plugin and
  QML module. No launcher and no `PATH` tricks: it's a Windows private assembly.
- **Release packages.** The target `MyApp_package` builds `MyApp-<version>-win64.zip`, ready to attach to a
  GitHub release. No Python or other tools needed, only CMake and `windeployqt`.
- **A `VERSIONINFO`** with your project version and icon on both exes.
- **An app-side API** to check for updates in-process and launch the updater.

Requires Qt 6.5+, CMake 3.22+, C++20 and Windows 10 1803+ (for the built-in `tar.exe`). Tested with MinGW; the MSVC path
in `qau_setup` is there but untested.

## Adding to a project

```bash
git submodule add https://github.com/Stalker72742/QAppUpdater.git third_party/QAppUpdater
```

```cmake
add_subdirectory(third_party/QAppUpdater)

qt_add_executable(MyApp main.cpp ...)
qau_setup(MyApp NAME MyApp GITHUB_REPO me/MyApp ICON app.ico PROTECTED_PATHS Saved)
```

`qau_setup` links `QAppUpdater::Core` to the app, so the app-side headers are available right away.

### `qau_setup` options

| Option | Default | Description |
|---|---|---|
| `NAME` | target name | Display name; also the package prefix `<NAME>-<version>-win64.zip` |
| `VERSION` | `PROJECT_VERSION` | Version written to `VERSIONINFO` and the package; numbers only (`1.2.3`) |
| `GITHUB_REPO` | — | `owner/name` the updater checks by default |
| `ICON` | — | `.ico` for both exes |
| `ACCENT_COLOR` | `#4C8DFF` | Accent of the updater window |
| `COMPANY`, `DESCRIPTION` | `NAME` | `VERSIONINFO` strings |
| `UPDATER_NAME` | `Updater` | File name of the updater exe |
| `RUNTIME_DIR` | `bin` | Folder with the DLLs, plugins and QML modules |
| `PROTECTED_PATHS` | — | Top-level user data the updater must never touch, e.g. `Saved Config` |
| `QML_DIR` | — | QML sources for `windeployqt` to scan |
| `WINDEPLOYQT_ARGS` | — | Extra `windeployqt` arguments, e.g. `--no-opengl-sw` |
| `DIST_DIR` | `<build>/dist` | Where `<target>_deploy` puts a standalone copy |
| `PACKAGE_DIR` | `<build>/packages` | Where `<target>_package` puts the zip |
| `NOTES_FILE` | — | Markdown release notes stored in the package (shown for local builds) |

Always protected besides `PROTECTED_PATHS`: `update/`, `updater.json`, `package.json`, `logs/`.

### Targets

| Target | What it does |
|---|---|
| `<app>` | After every build lays out `<build>/bin`, so the exe runs from the build folder |
| `<app>_deploy` | A clean, standalone copy in `DIST_DIR` with `package.json` |
| `<app>_package` | `<app>_deploy` plus `PACKAGE_DIR/<NAME>-<version>-win64.zip` |

Build packages from a Release build tree.

## In the app

```cpp
#include <QAppUpdater/AppControlServer.h>
#include <QAppUpdater/InstallLayout.h>
#include <QAppUpdater/ReleaseFinder.h>

using namespace QAppUpdater;

// Once at startup: delete files of the previous version that were in use during the update.
InstallLayout::removeLeftovers(InstallLayout::rootDir());

// Lets the updater close the running app before replacing its files. Save everything and quit.
connect(new AppControlServer(this), &AppControlServer::quitRequested, qApp, &QCoreApplication::quit);

// Check for updates in-process, with the same settings the updater uses.
auto* finder = new ReleaseFinder(this);
connect(finder, &ReleaseFinder::found, this, [](ReleaseInfo const& release) {
    if (InstallLayout::isNewer(release.version, InstallLayout::installedVersion(InstallLayout::rootDir())))
        InstallLayout::startUpdater(InstallLayout::rootDir(), {"--install"});
});
UpdaterConfig config;
UpdaterConfig::load(UpdaterConfig::defaultPath(), config);
finder->find(config);
```

`startUpdater()` arguments:
- none — open the updater window;
- `--install` — install as soon as a newer version is found;
- `--path <folder or zip> --install` — install a local build.

## The updater

```
Updater                          open the window
Updater --check                  exit code 2 if an update is available
Updater --update [--force]       install the latest version (closes the running app)
Updater --path <dir|zip> ...     use a local build for this run
Updater --help                   all options and the updater.json format
```

Settings live in `updater.json` next to the exe; the window's "Change source…" page edits it:

```json
{
    "source": "github",
    "github": { "repo": "owner/name", "prerelease": false },
    "local": { "path": "D:/builds/dist" },
    "restartApp": true
}
```

`source: "local"` takes a folder made by `<app>_deploy`, or a package zip. Logs go to `logs/updater.log`.

## Publishing a release

1. Bump `project(... VERSION)`.
2. Build `<app>_package` in a Release build.
3. Create a GitHub release tagged `v<version>` and attach `<NAME>-<version>-win64.zip`.

## How updating works

1. **Find.** GitHub releases API (`releases/latest`, or the list with pre-releases), or `package.json` of a local
   folder or zip. The installed version is the app exe's `VERSIONINFO`, so a hand-copied exe can't lie.
2. **Download** into `update/`, checking the size and, when GitHub reports it, the SHA-256.
3. **Unpack** with Windows' built-in `tar.exe` into `update/staging`.
4. **Validate** `package.json`. Every listed file must be a safe relative path outside user data. The package's
   `minUpdaterProtocol` must not exceed this updater's protocol; otherwise the user is told to install it by hand.
5. **Close the app.** The app's `AppControlServer` (a `QLocalServer` per install folder) reports its pid and quits
   on request; the updater waits for the process to exit.
6. **Replace files.** Each changed file is first renamed aside (`X.old`), then the new one is copied in. Files the
   old package listed and the new one doesn't are renamed aside too, and `package.json` is written last. Any
   failure renames everything back. Renaming works even on the running updater's own exe and DLLs, which is how it
   updates itself; backups it can't delete yet are removed on the next start (`removeLeftovers`).
7. **Restart** the app, unless `restartApp` is off.

### The `bin/` layout

Both exes embed a manifest that depends on a private assembly named after `RUNTIME_DIR`.
`bin/bin.manifest` lists every DLL in the folder, and the Windows loader resolves them from there before `main()`.
An embedded `qt.conf` (`:/qt/etc/qt.conf`) points Qt at `bin/plugins` and `bin/qml`.

- The manifest is regenerated from the folder's contents on every deploy, so new DLLs (QML modules, FFmpeg, …)
  just work.
- Never put a `bin.manifest` next to the exe: the loader would then look for the DLLs in the exe folder.
- The updater must sit next to the app exe: a private assembly is only looked up in a subfolder of the exe's
  own folder.

## License

Public domain ([Unlicense](LICENSE)).
