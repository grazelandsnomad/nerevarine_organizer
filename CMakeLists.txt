cmake_minimum_required(VERSION 3.16)
project(NerevarineOrganizer VERSION 0.3.1 LANGUAGES CXX)

# C++23 covers everything actually used here (std::expected, std::println).
# C++26 was tripping Qt's q26numeric.h polyfill on the MSYS2 UCRT64 CI toolchain
# (`'saturate_cast' has not been declared in 'std'`) - the libstdc++ shipped
# with the Windows runner doesn't expose `std::saturate_cast` even with
# `-std=gnu++26`, and Qt's header imports it unconditionally in C++26 mode.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()
add_subdirectory(tests)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Concurrent)

# Optional: qt6keychain for secure API-key storage (libsecret / KWallet /
# DPAPI backend).  When present, the Nexus API key is stored there instead
# of in plain-text QSettings.  When absent, the app falls back to QSettings
# so builds don't break on systems without the library.
find_package(Qt6Keychain QUIET)
if(Qt6Keychain_FOUND)
    message(STATUS "qt6keychain found - API key will be stored in the system keyring.")
else()
    message(STATUS "qt6keychain NOT found - API key falls back to plain-text QSettings.")
endif()

# Some distros (Arch/CachyOS) ship Qt6 compiled with -mno-direct-extern-access
# and propagate the flag through their mkspecs. It's GCC-only; clang
# (and therefore a clang-based build or clangd for LSP) errors on it.
# Strip it whenever the compiler isn't GCC so a clang build works too.
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    string(REPLACE "-mno-direct-extern-access" ""
           CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REPLACE "-mno-direct-extern-access" ""
           CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
endif()

set(SOURCES
    main.cpp
    src/mainwindow.cpp
    src/separatordialog.cpp
    src/modlistdelegate.cpp
    src/translator.cpp
    src/fomodwizard.cpp
    src/fomod_copy.cpp
    src/fomod_install.cpp
    src/fomod_path.cpp
    src/load_order_merge.cpp
    src/modentry.cpp
    src/modentry_item.cpp
    src/pluginparser.cpp
    src/firstrunwizard.cpp
    src/openmwconfigwriter.cpp
    src/log_triage.cpp
    src/plugin_collisions.cpp
    src/asset_collisions.cpp
    src/modlist_sync_guard.cpp
    src/settings_migrations.cpp
    src/master_satisfaction.cpp
    src/bsareader.cpp
    src/safe_fs.cpp
    src/deps_resolver.cpp
    src/installcontroller.cpp
    src/install_layout.cpp
    src/loadordercontroller.cpp
    src/nexusclient.cpp
    src/nexuscontroller.cpp
    src/nxmurl.cpp
    src/downloadqueue.cpp
    src/undo_stack.cpp
    src/zoom_controller.cpp
    src/filter_bar.cpp
    src/notify_banner.cpp
    src/column_header.cpp
    src/forbidden_mods.cpp
    src/game_profiles.cpp
    src/ini_doc.cpp
    src/nexus_name.cpp
    src/conflict_scan.cpp
    src/reboot_check.cpp
    src/toolbar_customization.cpp
    src/scan_coordinator.cpp
    src/backup_manager.cpp
    src/bulk_install_queue.cpp
    src/review_updates_dialog.cpp
    src/launch_warnings.cpp
    src/modlist_sort.cpp
    src/send_to_dialog.cpp
    src/logging.cpp
    include/mainwindow.h
    include/undo_stack.h
    include/zoom_controller.h
    include/filter_bar.h
    include/notify_banner.h
    include/column_header.h
    include/forbidden_mods.h
    include/game_profiles.h
    include/ini_doc.h
    include/nexus_name.h
    include/conflict_scan.h
    include/reboot_check.h
    include/toolbar_customization.h
    include/scan_coordinator.h
    include/backup_manager.h
    include/bulk_install_queue.h
    include/review_updates_dialog.h
    include/launch_warnings.h
    include/modlist_sort.h
    include/send_to_dialog.h
    include/logging.h
    include/downloadqueue.h
    include/installcontroller.h
    include/install_layout.h
    include/loadordercontroller.h
    include/nexusclient.h
    include/nexuscontroller.h
    include/nxmurl.h
    include/separatordialog.h
    include/modlistdelegate.h
    include/fomodwizard.h
    include/fomod_install.h
    include/fomod_path.h
    include/load_order_merge.h
    include/pluginparser.h
    include/firstrunwizard.h
    include/fs_utils.h
    include/openmwconfigwriter.h
    include/master_satisfaction.h
    include/bsareader.h
    include/safe_fs.h
    include/deps_resolver.h
    resources.qrc
)

# Platform-specific executable flags (Linux + Windows only; macOS not supported)
if(APPLE)
    message(FATAL_ERROR
        "macOS is not a supported target. Build on Linux or Windows.")
endif()

if(WIN32)
    # WIN32 suppresses the console window on Windows
    add_executable(nerevarine_organizer WIN32 ${SOURCES})
else()
    add_executable(nerevarine_organizer ${SOURCES})
endif()

target_include_directories(nerevarine_organizer PRIVATE include)

target_link_libraries(nerevarine_organizer PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
)

# GCC 14 keeps std::print / std::println symbols in libstdc++exp.a;
# GCC 15 promoted them into libstdc++ on Linux but still keeps the
# Windows terminal helpers (__open_terminal, __write_to_terminal)
# in libstdc++exp.a on MinGW. So we link stdc++exp on GCC 14 (Linux
# CI) and on Windows up through 15.x.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
        AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "14"
        AND (WIN32 OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS "15"))
    target_link_libraries(nerevarine_organizer PRIVATE stdc++exp)
endif()
if(Qt6Keychain_FOUND)
    target_link_libraries(nerevarine_organizer PRIVATE Qt6Keychain::Qt6Keychain)
    target_compile_definitions(nerevarine_organizer PRIVATE HAVE_QTKEYCHAIN)
endif()

# Windows: link extra system libraries required by Qt6 static builds (MXE)
if(WIN32)
    target_link_libraries(nerevarine_organizer PRIVATE
        ws2_32 winmm imm32 ole32 oleaut32 uuid dwmapi
    )
endif()

# -- Post-build: copy runtime assets next to the binary ---

add_custom_command(TARGET nerevarine_organizer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/nerevarine_prefs.ini
        $<TARGET_FILE_DIR:nerevarine_organizer>/nerevarine_prefs.ini
    COMMENT "Copying nerevarine_prefs.ini to build directory"
)

add_custom_command(TARGET nerevarine_organizer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/translations
        $<TARGET_FILE_DIR:nerevarine_organizer>/translations
    COMMENT "Copying translations to build directory"
)

# -- Install rules ---

include(GNUInstallDirs)

install(TARGETS nerevarine_organizer
    BUNDLE  DESTINATION .                      # macOS: install .app at prefix root
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} # Linux/Windows: install binary
)

install(DIRECTORY translations
    DESTINATION ${CMAKE_INSTALL_DATADIR}/nerevarine_organizer
)

install(FILES nerevarine_prefs.ini
    DESTINATION ${CMAKE_INSTALL_DATADIR}/nerevarine_organizer
)

# Linux-only: .desktop entry and icon
if(UNIX)
    install(FILES nerevarine_organizer.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )

    install(FILES assets/icons/cystal_full_0.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
        RENAME nerevarine_organizer.png
    )

    # Refresh desktop and icon caches after install
    install(CODE "
        execute_process(COMMAND update-desktop-database
            \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_DATADIR}/applications\"
            ERROR_QUIET)
        execute_process(COMMAND gtk4-update-icon-cache -f -t
            \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor\"
            ERROR_QUIET)
    ")
endif()

# Windows: deploy Qt DLLs alongside the .exe (dynamic builds only)
# MXE static builds don't need this - the .exe is self-contained.
if(WIN32 AND NOT DEFINED MXE_TARGET)
    find_program(WINDEPLOYQT windeployqt6 windeployqt HINTS ${Qt6_DIR}/../../../bin)
    if(WINDEPLOYQT)
        install(CODE "
            execute_process(COMMAND \"${WINDEPLOYQT}\"
                --no-compiler-runtime
                \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/nerevarine_organizer.exe\"
                ERROR_QUIET)
        ")
    endif()
endif()

# macOS is not supported.
