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

# GStreamer: on macOS prefer the official framework (no pkgconfig there),
# fall back to pkg-config (Homebrew) on other platforms.
if(APPLE)
    set(_GST_FRAMEWORK "/Library/Frameworks/GStreamer.framework/Versions/1.0")
    if(EXISTS "${_GST_FRAMEWORK}/include/gstreamer-1.0/gst/gst.h")
        set(_GST_PREFIX "${_GST_FRAMEWORK}")
    else()
        # fall back to Homebrew GStreamer
        execute_process(COMMAND brew --prefix gstreamer
            OUTPUT_VARIABLE _GST_BREW OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(NOT _GST_BREW)
            set(_GST_BREW "/usr/local/opt/gstreamer")
        endif()
        set(_GST_PREFIX "${_GST_BREW}")
    endif()

    find_library(GSTREAMER_LIB gstreamer-1.0 PATHS "${_GST_PREFIX}/lib" NO_DEFAULT_PATH REQUIRED)
    find_library(GLIB_LIB     glib-2.0       PATHS "${_GST_PREFIX}/lib" NO_DEFAULT_PATH REQUIRED)
    find_library(GOBJECT_LIB  gobject-2.0    PATHS "${_GST_PREFIX}/lib" NO_DEFAULT_PATH REQUIRED)

    set(GSTREAMER_INCLUDE_DIRS
        "${_GST_PREFIX}/include/gstreamer-1.0"
        "${_GST_PREFIX}/include/glib-2.0"
        "${_GST_PREFIX}/lib/glib-2.0/include"
    )
    # Brew glib puts glibconfig.h under lib/glib-2.0/include too; add brew fallback
    execute_process(COMMAND brew --prefix OUTPUT_VARIABLE _BREW_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_BREW_PREFIX AND EXISTS "${_BREW_PREFIX}/lib/glib-2.0/include/glibconfig.h")
        list(APPEND GSTREAMER_INCLUDE_DIRS "${_BREW_PREFIX}/lib/glib-2.0/include")
    endif()

    set(GSTREAMER_LIBRARIES "${GSTREAMER_LIB}" "${GLIB_LIB}" "${GOBJECT_LIB}")
    set(GSTREAMER_LDFLAGS_OTHER "-Wl,-rpath,${_GST_PREFIX}/lib")

    message(STATUS "Using GStreamer from ${_GST_PREFIX}")
    unset(_GST_FRAMEWORK)
    unset(_GST_PREFIX)
    unset(_GST_BREW)
    unset(_BREW_PREFIX)
else()
    pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
endif()

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
    src/player.cpp
    src/vumeter.cpp
    src/playerwidget.cpp
    src/qled.cpp
    src/playlistitem.cpp
    src/playlist.cpp
    src/progressbar.cpp
    src/collectiondb.cpp
    src/settingsdialog.cpp
    src/track.cpp
    src/trackanalyser.cpp
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
    src/monitorplayer.cpp
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
    src/trackanalyser.h
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
    GST_API_VERSION_1
)

target_include_directories(knowthelist PRIVATE
    src
    ${GSTREAMER_INCLUDE_DIRS}
    ${TAGLIB_INCLUDE_DIRS}
)

target_link_libraries(knowthelist PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Xml
    Qt6::Sql
    Qt6::Concurrent
    ${GSTREAMER_LIBRARIES}
    ${TAGLIB_LIBRARIES}
)

target_link_options(knowthelist PRIVATE
    ${GSTREAMER_LDFLAGS_OTHER}
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
