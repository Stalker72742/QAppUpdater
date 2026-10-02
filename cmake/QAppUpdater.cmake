# qau_setup(<app target>
#     [NAME <display name>]            default: the target name; also the package prefix
#     [VERSION <x.y.z[.w]>]            default: PROJECT_VERSION
#     [GITHUB_REPO <owner/name>]       default release source
#     [ICON <file.ico>]                icon of both exes
#     [ACCENT_COLOR <#RRGGBB>]         updater window accent
#     [COMPANY <text>]
#     [DESCRIPTION <text>]             FileDescription of the app exe
#     [UPDATER_NAME <name>]            default: Updater (-> Updater.exe)
#     [RUNTIME_DIR <folder>]           default: bin
#     [PROTECTED_PATHS <name>...]      top-level user data the updater must never touch (e.g. Saved Config)
#     [QML_DIR <dir>]                  QML sources for windeployqt to scan
#     [WINDEPLOYQT_ARGS <arg>...]
#     [DIST_DIR <dir>]                 default: <build>/dist
#     [PACKAGE_DIR <dir>]              default: <build>/packages
#     [NOTES_FILE <file.md>])          optional release notes put into package.json
#
# Creates:
#   QAppUpdater::Core   app-side API (AppControlServer, InstallLayout, ReleaseFinder, UpdaterConfig); linked to the app
#   <UPDATER_NAME>      the updater exe, built next to the app exe
#   both exes get a VERSIONINFO, the icon, a manifest depending on the private assembly <RUNTIME_DIR> and an
#   embedded qt.conf; after every build of the app the runtime is laid out in <build>/<RUNTIME_DIR>
#   <app>_deploy        a clean, standalone copy in DIST_DIR with package.json
#   <app>_package       the same, plus DIST_DIR zipped into PACKAGE_DIR/<NAME>-<VERSION>-win64.zip

set(QAU_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." CACHE INTERNAL "")

function(qau_setup app_target)
    cmake_parse_arguments(PARSE_ARGV 1 arg ""
        "NAME;VERSION;GITHUB_REPO;ICON;ACCENT_COLOR;COMPANY;DESCRIPTION;UPDATER_NAME;RUNTIME_DIR;QML_DIR;DIST_DIR;PACKAGE_DIR;NOTES_FILE"
        "PROTECTED_PATHS;WINDEPLOYQT_ARGS")

    if (NOT TARGET ${app_target})
        message(FATAL_ERROR "qau_setup: ${app_target} is not a target")
    endif ()
    if (TARGET QAppUpdaterCore)
        message(FATAL_ERROR "qau_setup: can only be called once per project")
    endif ()

    set(QAU_NAME "${arg_NAME}")
    if (NOT QAU_NAME)
        set(QAU_NAME "${app_target}")
    endif ()
    set(QAU_VERSION "${arg_VERSION}")
    if (NOT QAU_VERSION)
        set(QAU_VERSION "${PROJECT_VERSION}")
    endif ()
    if (NOT QAU_VERSION MATCHES "^[0-9]+(\\.[0-9]+)*$")
        message(FATAL_ERROR "qau_setup: version \"${QAU_VERSION}\" must be numbers separated by dots")
    endif ()
    set(QAU_UPDATER_NAME "${arg_UPDATER_NAME}")
    if (NOT QAU_UPDATER_NAME)
        set(QAU_UPDATER_NAME Updater)
    endif ()
    set(QAU_RUNTIME_DIR "${arg_RUNTIME_DIR}")
    if (NOT QAU_RUNTIME_DIR)
        set(QAU_RUNTIME_DIR bin)
    endif ()
    if (NOT QAU_RUNTIME_DIR MATCHES "^[A-Za-z0-9_.-]+$")
        message(FATAL_ERROR "qau_setup: RUNTIME_DIR must be a single folder name, got \"${QAU_RUNTIME_DIR}\"")
    endif ()
    set(QAU_GITHUB_REPO "${arg_GITHUB_REPO}")
    set(QAU_ACCENT_COLOR "${arg_ACCENT_COLOR}")
    set(QAU_COMPANY "${arg_COMPANY}")
    if (NOT QAU_COMPANY)
        set(QAU_COMPANY "${QAU_NAME}")
    endif ()
    set(app_description "${arg_DESCRIPTION}")
    if (NOT app_description)
        set(app_description "${QAU_NAME}")
    endif ()

    get_target_property(app_output_name ${app_target} OUTPUT_NAME)
    if (NOT app_output_name)
        set(app_output_name ${app_target})
    endif ()
    set(QAU_APP_EXE "${app_output_name}.exe")
    set(QAU_UPDATER_EXE "${QAU_UPDATER_NAME}.exe")

    set(QAU_PROTECTED_LIST "")
    foreach (path ${arg_PROTECTED_PATHS})
        if (QAU_PROTECTED_LIST)
            string(APPEND QAU_PROTECTED_LIST ", ")
        endif ()
        string(APPEND QAU_PROTECTED_LIST "QStringLiteral(\"${path}\")")
    endforeach ()

    string(REPLACE "." ";" version_parts "${QAU_VERSION}")
    list(APPEND version_parts 0 0 0 0)
    list(SUBLIST version_parts 0 4 version_parts)
    list(JOIN version_parts "," QAU_VERSION_COMMAS)

    set(gen "${CMAKE_CURRENT_BINARY_DIR}/qau")
    file(MAKE_DIRECTORY "${gen}")
    configure_file("${QAU_ROOT_DIR}/templates/AppInfo.cpp.in" "${gen}/AppInfo.cpp" @ONLY)
    configure_file("${QAU_ROOT_DIR}/templates/app.manifest.in" "${gen}/app.manifest" @ONLY)
    configure_file("${QAU_ROOT_DIR}/templates/qt.conf.in" "${gen}/qt.conf" @ONLY)

    set(QAU_ICON_LINE "")
    if (arg_ICON)
        get_filename_component(icon "${arg_ICON}" ABSOLUTE)
        configure_file("${icon}" "${gen}/app.ico" COPYONLY)
        set(QAU_ICON_LINE "IDI_ICON1 ICON \"app.ico\"")
    endif ()

    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network Widgets)

    # --- App-side library
    add_library(QAppUpdaterCore STATIC
        "${QAU_ROOT_DIR}/include/QAppUpdater/AppInfo.h"
        "${QAU_ROOT_DIR}/include/QAppUpdater/AppControlServer.h"
        "${QAU_ROOT_DIR}/include/QAppUpdater/InstallLayout.h"
        "${QAU_ROOT_DIR}/include/QAppUpdater/ReleaseFinder.h"
        "${QAU_ROOT_DIR}/include/QAppUpdater/UpdaterConfig.h"
        "${QAU_ROOT_DIR}/src/AppControlServer.cpp"
        "${QAU_ROOT_DIR}/src/InstallLayout.cpp"
        "${QAU_ROOT_DIR}/src/ReleaseFinder.cpp"
        "${QAU_ROOT_DIR}/src/SystemTools.h"
        "${QAU_ROOT_DIR}/src/SystemTools.cpp"
        "${QAU_ROOT_DIR}/src/UpdaterConfig.cpp"
        "${QAU_ROOT_DIR}/src/UpdateInstaller.h"
        "${QAU_ROOT_DIR}/src/UpdateInstaller.cpp"
        "${gen}/AppInfo.cpp")
    add_library(QAppUpdater::Core ALIAS QAppUpdaterCore)
    set_target_properties(QAppUpdaterCore PROPERTIES AUTOMOC ON)
    target_compile_features(QAppUpdaterCore PUBLIC cxx_std_20)
    target_include_directories(QAppUpdaterCore
        PUBLIC "${QAU_ROOT_DIR}/include"
        PRIVATE "${QAU_ROOT_DIR}/src")
    target_link_libraries(QAppUpdaterCore PUBLIC Qt6::Core Qt6::Network)
    target_compile_definitions(QAppUpdaterCore PUBLIC QAU_UPDATER_PROTOCOL=${QAU_UPDATER_PROTOCOL})
    if (WIN32)
        target_link_libraries(QAppUpdaterCore PRIVATE version) # GetFileVersionInfo
    endif ()

    # --- Updater exe (a console program, so its CLI can print; the window mode drops the console)
    add_executable(${QAU_UPDATER_NAME}
        "${QAU_ROOT_DIR}/updater/main.cpp"
        "${QAU_ROOT_DIR}/updater/Logging.h"
        "${QAU_ROOT_DIR}/updater/Logging.cpp"
        "${QAU_ROOT_DIR}/updater/UpdaterPalette.h"
        "${QAU_ROOT_DIR}/updater/UpdaterPalette.cpp"
        "${QAU_ROOT_DIR}/updater/UpdaterWindow.h"
        "${QAU_ROOT_DIR}/updater/UpdaterWindow.cpp")
    set_target_properties(${QAU_UPDATER_NAME} PROPERTIES AUTOMOC ON)
    target_include_directories(${QAU_UPDATER_NAME} PRIVATE "${QAU_ROOT_DIR}/src" "${QAU_ROOT_DIR}/updater")
    target_link_libraries(${QAU_UPDATER_NAME} PRIVATE QAppUpdaterCore Qt6::Widgets)
    # Next to the app exe: the private assembly is only looked up in a subfolder of the exe's own folder.
    get_target_property(app_runtime_output ${app_target} RUNTIME_OUTPUT_DIRECTORY)
    if (app_runtime_output)
        set_target_properties(${QAU_UPDATER_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${app_runtime_output}")
    endif ()

    # --- Per-exe resources and embedded qt.conf
    foreach (exe_target ${app_target} ${QAU_UPDATER_NAME})
        if (exe_target STREQUAL app_target)
            set(QAU_EXE_NAME "${app_output_name}")
            set(QAU_EXE_DESCRIPTION "${app_description}")
        else ()
            set(QAU_EXE_NAME "${QAU_UPDATER_NAME}")
            set(QAU_EXE_DESCRIPTION "${QAU_NAME} Updater")
        endif ()
        configure_file("${QAU_ROOT_DIR}/templates/app.rc.in" "${gen}/${QAU_EXE_NAME}.rc" @ONLY)

        if (MINGW)
            # windres forwards the target's include dirs to its preprocessor unquoted and breaks on paths
            # with spaces, so the resources are compiled by hand from the generated folder.
            set(obj "${gen}/${QAU_EXE_NAME}.rc.obj")
            add_custom_command(OUTPUT "${obj}"
                COMMAND ${CMAKE_RC_COMPILER} -O coff "${QAU_EXE_NAME}.rc" "${obj}"
                DEPENDS "${gen}/${QAU_EXE_NAME}.rc" "${gen}/app.manifest"
                WORKING_DIRECTORY "${gen}"
                VERBATIM)
            target_sources(${exe_target} PRIVATE "${obj}")
        elseif (WIN32)
            target_sources(${exe_target} PRIVATE "${gen}/${QAU_EXE_NAME}.rc")
            target_link_options(${exe_target} PRIVATE /MANIFEST:NO)
        endif ()

        qt_add_resources(${exe_target} "qau_qtconf" PREFIX "/qt/etc" BASE "${gen}" FILES "${gen}/qt.conf")
    endforeach ()

    target_link_libraries(${app_target} PRIVATE QAppUpdaterCore)
    add_dependencies(${app_target} ${QAU_UPDATER_NAME})

    # --- Runtime layout and packaging
    get_target_property(qmake_location Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(qt_bin "${qmake_location}" DIRECTORY)
    find_program(QAU_WINDEPLOYQT windeployqt HINTS "${qt_bin}" REQUIRED)
    set(mingw_bin "")
    if (MINGW)
        get_filename_component(mingw_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    endif ()

    set(dist_dir "${arg_DIST_DIR}")
    if (NOT dist_dir)
        set(dist_dir "${CMAKE_BINARY_DIR}/dist")
    endif ()
    set(package_dir "${arg_PACKAGE_DIR}")
    if (NOT package_dir)
        set(package_dir "${CMAKE_BINARY_DIR}/packages")
    endif ()

    # Everything the deploy script needs, in a generated file: passing it on the command line breaks in
    # cmd.exe as soon as paths contain spaces.
    set(config_file "${gen}/deploy-$<CONFIG>.cmake")
    file(GENERATE OUTPUT "${config_file}" CONTENT "set(QAU_EXES \"$<TARGET_FILE:${app_target}>;$<TARGET_FILE:${QAU_UPDATER_NAME}>\")
set(QAU_RUNTIME_DEST \"$<TARGET_FILE_DIR:${app_target}>\")
set(QAU_DIST_DIR \"${dist_dir}\")
set(QAU_PACKAGE_DIR \"${package_dir}\")
set(QAU_RUNTIME_DIR \"${QAU_RUNTIME_DIR}\")
set(QAU_WINDEPLOYQT \"${QAU_WINDEPLOYQT}\")
set(QAU_TEMPLATE_DIR \"${QAU_ROOT_DIR}/templates\")
set(QAU_QML_DIR \"${arg_QML_DIR}\")
set(QAU_MINGW_BIN \"${mingw_bin}\")
set(QAU_PROTECTED \"${arg_PROTECTED_PATHS}\")
set(QAU_WINDEPLOYQT_ARGS \"${arg_WINDEPLOYQT_ARGS}\")
set(QAU_NAME \"${QAU_NAME}\")
set(QAU_VERSION \"${QAU_VERSION}\")
set(QAU_PROTOCOL \"${QAU_UPDATER_PROTOCOL}\")
set(QAU_NOTES_FILE \"${arg_NOTES_FILE}\")
")
    set(deploy_script "${QAU_ROOT_DIR}/cmake/QauDeploy.cmake")

    add_custom_command(TARGET ${app_target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -DQAU_MODE=runtime "-DQAU_CONFIG=${config_file}" -P "${deploy_script}"
        COMMENT "Laying out ${QAU_RUNTIME_DIR}/ for ${QAU_NAME}"
        VERBATIM)

    add_custom_target(${app_target}_deploy
        COMMAND "${CMAKE_COMMAND}" -DQAU_MODE=deploy "-DQAU_CONFIG=${config_file}" -P "${deploy_script}"
        DEPENDS ${app_target} ${QAU_UPDATER_NAME}
        COMMENT "Deploying ${QAU_NAME} ${QAU_VERSION} to ${dist_dir}"
        USES_TERMINAL
        VERBATIM)

    add_custom_target(${app_target}_package
        COMMAND "${CMAKE_COMMAND}" -DQAU_MODE=package "-DQAU_CONFIG=${config_file}" -P "${deploy_script}"
        DEPENDS ${app_target} ${QAU_UPDATER_NAME}
        COMMENT "Packaging ${QAU_NAME} ${QAU_VERSION} into ${package_dir}"
        USES_TERMINAL
        VERBATIM)
endfunction()
