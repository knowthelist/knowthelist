cmake_minimum_required(VERSION 3.16)

project(knowthelist VERSION 2.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# macOS: auto-detect Qt6 installed via aqtinstall or Qt online installer
# when CMAKE_PREFIX_PATH / Qt6_DIR have not been set explicitly.
if(APPLE AND NOT DEFINED Qt6_DIR)
    set(_qt6_search_roots
        "$ENV{HOME}/Qt"
        "/opt/Qt"
    )
    foreach(_root IN LISTS _qt6_search_roots)
        file(GLOB _candidates "${_root}/6.*/macos/lib/cmake/Qt6")
        if(_candidates)
            list(SORT _candidates ORDER DESCENDING)
            list(GET _candidates 0 _qt6_cmake_dir)
            get_filename_component(_qt6_prefix "${_qt6_cmake_dir}/../../.." ABSOLUTE)
            list(PREPEND CMAKE_PREFIX_PATH "${_qt6_prefix}")
            message(STATUS "Auto-detected Qt6 at ${_qt6_prefix}")
            break()
        endif()
    endforeach()
    unset(_qt6_search_roots)
    unset(_candidates)
    unset(_qt6_cmake_dir)
    unset(_qt6_prefix)
endif()

# In a Flatpak sandbox, Qt6 CMake config files usually live in /run/host/usr
# and are not discoverable by default.
if(DEFINED ENV{FLATPAK_ID} AND IS_DIRECTORY /run/host/usr)
    list(APPEND CMAKE_PREFIX_PATH "/run/host/usr")

    if(NOT DEFINED Qt6_DIR)
        set(_QT6_CONFIG_CANDIDATES
            "/run/host/usr/lib/cmake/Qt6/Qt6Config.cmake"
            "/run/host/usr/lib64/cmake/Qt6/Qt6Config.cmake"
            "/run/host/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/cmake/Qt6/Qt6Config.cmake"
            "/run/host/usr/lib/x86_64-linux-gnu/cmake/Qt6/Qt6Config.cmake"
        )

        foreach(_qt6_config IN LISTS _QT6_CONFIG_CANDIDATES)
            if(EXISTS "${_qt6_config}")
                get_filename_component(Qt6_DIR "${_qt6_config}" DIRECTORY)
                set(Qt6_DIR "${Qt6_DIR}" CACHE PATH "Qt6 CMake package directory")
                break()
            endif()
        endforeach()

        unset(_QT6_CONFIG_CANDIDATES)
        unset(_qt6_config)
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Xml Sql Concurrent)

if(COMMAND qt_standard_project_setup)
    qt_standard_project_setup()
endif()

# JUCE: fetch from GitHub if not found locally
include(FetchContent)

# Keep FetchContent downloads outside the build directory so `rm -rf build`
# does not force re-downloading large dependencies like JUCE.
if(DEFINED ENV{XDG_CACHE_HOME})
    set(_KTL_CACHE_ROOT "$ENV{XDG_CACHE_HOME}")
elseif(DEFINED ENV{LOCALAPPDATA})
    set(_KTL_CACHE_ROOT "$ENV{LOCALAPPDATA}")
elseif(DEFINED ENV{HOME})
    set(_KTL_CACHE_ROOT "$ENV{HOME}/.cache")
else()
    set(_KTL_CACHE_ROOT "${CMAKE_SOURCE_DIR}/.cache")
endif()
set(KNOWTHELIST_FETCHCONTENT_DIR
    "${_KTL_CACHE_ROOT}/knowthelist/fetchcontent"
    CACHE PATH "Persistent cache directory for FetchContent dependencies")
set(FETCHCONTENT_BASE_DIR "${KNOWTHELIST_FETCHCONTENT_DIR}")
unset(_KTL_CACHE_ROOT)

# Avoid large re-downloads on reconfigure and reduce clone size.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# Try to find JUCE installed system-wide first
find_package(JUCE QUIET CONFIG)

if(NOT JUCE_FOUND)
    # Fetch JUCE from GitHub (version 7.0.x)
    message(STATUS "JUCE not found locally; fetching from GitHub...")
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 7.0.11
        GIT_SHALLOW TRUE
        GIT_PROGRESS ON
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# Flatpak-specific setup: the Qt6 host tools and pkg-config cannot run
# without their host libraries. Pin the Flatpak's own pkg-config and point
# it at the host .pc files. Qt tool wrappers are set after qt_add_executable.
if(DEFINED ENV{FLATPAK_ID} AND IS_DIRECTORY /run/host/usr)
    set(PKG_CONFIG_EXECUTABLE /usr/bin/pkg-config CACHE FILEPATH
        "pkg-config executable" FORCE)
    set(ENV{PKG_CONFIG_PATH}
        "/run/host/usr/lib/x86_64-linux-gnu/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    set_target_properties(Qt6::uic PROPERTIES IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/cmake/flatpak-wrappers/uic")
    set_target_properties(Qt6::moc PROPERTIES IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/cmake/flatpak-wrappers/moc")
    set_target_properties(Qt6::rcc PROPERTIES IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/cmake/flatpak-wrappers/rcc")
endif()
find_package(PkgConfig REQUIRED)

# TagLib: on macOS use explicit brew opt path to avoid GStreamer framework's
# bundled TagLib 1.x taking precedence. On other platforms try native CMake
# config first, fall back to pkg-config.
if(APPLE)
    execute_process(COMMAND brew --prefix taglib
        OUTPUT_VARIABLE _TAGLIB_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _TAGLIB_PREFIX)
        set(_TAGLIB_PREFIX "/usr/local/opt/taglib")
    endif()
    find_library(TAGLIB_LIB tag PATHS "${_TAGLIB_PREFIX}/lib" NO_DEFAULT_PATH REQUIRED)
    find_path(TAGLIB_INC taglib/tag.h PATHS "${_TAGLIB_PREFIX}/include" NO_DEFAULT_PATH REQUIRED)
    set(TAGLIB_INCLUDE_DIRS "${TAGLIB_INC}")
    set(TAGLIB_LIBRARIES "${TAGLIB_LIB}")
    unset(_TAGLIB_PREFIX)
    unset(TAGLIB_LIB)
    unset(TAGLIB_INC)
else()
    find_package(TagLib QUIET)
    if(NOT TagLib_FOUND)
        pkg_check_modules(TAGLIB REQUIRED taglib)
        set(TAGLIB_INCLUDE_DIRS ${TAGLIB_INCLUDE_DIRS})
        set(TAGLIB_LIBRARIES ${TAGLIB_LIBRARIES})
    else()
        set(TAGLIB_INCLUDE_DIRS "")
        set(TAGLIB_LIBRARIES TagLib::TagLib)
    endif()
endif()

set(SOURCES
    src/main.cpp
    src/knowthelist.cpp
    src/beatsyncwidget.cpp
    src/playerbpmwidget.cpp
    src/player.cpp
    src/audio_backend.h
    src/juce_audio_backend.cpp
    src/monitorplayer.cpp
    src/monitor_audio_backend.cpp
    src/vumeter.cpp
    src/playerwidget.cpp
    src/qled.cpp
    src/playlistitem.cpp
    src/playlist.cpp
    src/progressbar.cpp
    src/collectiondb.cpp
    src/settingsdialog.cpp
    src/track.cpp
    src/trackanalyzer.cpp
    src/djsession.cpp
    src/dj.cpp
    src/filter.cpp
    src/djwidget.cpp
    src/djfilterwidget.cpp
    src/fancytabwidget.cpp
    src/stylehelper.cpp
    src/filebrowser.cpp
    src/collectionwidget.cpp
    src/collectiontree.cpp
    src/collectionupdater.cpp
    src/collectiontreeitem.cpp
    src/collectionsetupmodel.cpp
    src/stackdisplay.cpp
    src/djsettings.cpp
    src/modeselector.cpp
    src/playlistbrowser.cpp
    src/playlistwidget.cpp
    src/djbrowser.cpp
    src/ratingwidget.cpp
    src/customdial.cpp
)

set(HEADERS
    src/knowthelist.h
    src/beatsyncwidget.h
    src/playerbpmwidget.h
    src/vumeter.h
    src/playerwidget.h
    src/qled.h
    src/playlistitem.h
    src/playlist.h
    src/player.h
    src/progressbar.h
    src/collectiondb.h
    src/settingsdialog.h
    src/track.h
    src/trackanalyzer.h
    src/djsession.h
    src/dj.h
    src/filter.h
    src/djwidget.h
    src/djfilterwidget.h
    src/fancytabwidget.h
    src/stylehelper.h
    src/filebrowser.h
    src/collectionwidget.h
    src/collectiontree.h
    src/collectionupdater.h
    src/collectiontreeitem.h
    src/monitorplayer.h
    src/collectionsetupmodel.h
    src/stackdisplay.h
    src/djsettings.h
    src/modeselector.h
    src/playlistbrowser.h
    src/playlistwidget.h
    src/djbrowser.h
    src/ratingwidget.h
    src/customdial.h
)

set(FORMS
    src/settingsdialog.ui
    src/djwidget.ui
    src/djfilterwidget.ui
    src/playerwidget.ui
    src/knowthelist.ui
    src/djsettings.ui
    src/modeselector.ui
    src/playlistwidget.ui
)

set(RESOURCES
    images/icons.qrc
    locale/locale.qrc
)

qt_add_executable(knowthelist
    ${SOURCES}
    ${HEADERS}
    ${FORMS}
    ${RESOURCES}
)

# In Flatpak: redirect moc/uic/rcc to wrapper scripts that set LD_LIBRARY_PATH
if(DEFINED ENV{FLATPAK_ID} AND IS_DIRECTORY /run/host/usr)
    set(_QT_WRAPPER_DIR "${CMAKE_SOURCE_DIR}/cmake/flatpak-wrappers")
    set_target_properties(knowthelist PROPERTIES
        AUTOMOC_EXECUTABLE "${_QT_WRAPPER_DIR}/moc"
        AUTOUIC_EXECUTABLE "${_QT_WRAPPER_DIR}/uic"
        AUTORCC_EXECUTABLE "${_QT_WRAPPER_DIR}/rcc")
    unset(_QT_WRAPPER_DIR)
endif()

target_compile_definitions(knowthelist PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
)

target_include_directories(knowthelist PRIVATE
    src
    ${TAGLIB_INCLUDE_DIRS}
)

target_link_libraries(knowthelist PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Xml
    Qt6::Sql
    Qt6::Concurrent
    ${TAGLIB_LIBRARIES}
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
)

target_link_options(knowthelist PRIVATE
    ${TAGLIB_LDFLAGS_OTHER}
)

# Linux-specific: ALSA
if(UNIX AND NOT APPLE)
    pkg_check_modules(ALSA REQUIRED alsa)
    target_include_directories(knowthelist PRIVATE ${ALSA_INCLUDE_DIRS})
    target_link_libraries(knowthelist PRIVATE ${ALSA_LIBRARIES})
endif()

# macOS-specific
if(APPLE)
    find_library(COREAUDIO_LIBRARY CoreAudio REQUIRED)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation REQUIRED)
    target_link_libraries(knowthelist PRIVATE
        ${COREAUDIO_LIBRARY}
        ${COREFOUNDATION_LIBRARY}
    )
    set_target_properties(knowthelist PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_ICON_FILE headset.icns
    )
    target_sources(knowthelist PRIVATE dist/headset.icns)
    set_source_files_properties(dist/headset.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
    )
endif()

# Install rules (Linux)
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS knowthelist DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES dist/knowthelist.png DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps)
    install(FILES dist/Knowthelist.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
endif()
