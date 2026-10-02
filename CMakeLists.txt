cmake_minimum_required(VERSION 3.22)

project(QAppUpdater
        VERSION 1.0.0
        DESCRIPTION "Self-updating Qt 6 apps on Windows: updater exe, release packages, bin/ runtime layout"
        LANGUAGES CXX)

# Version of the package format / install procedure (QAppUpdater::kUpdaterProtocol). Every package states
# it as minUpdaterProtocol; an older updater refuses such a package. Bump only for changes an old updater
# would get wrong (package layout, how files are replaced, ...).
set(QAU_UPDATER_PROTOCOL 1 CACHE INTERNAL "")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/QAppUpdater.cmake")
