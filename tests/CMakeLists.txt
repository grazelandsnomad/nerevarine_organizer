find_package(Qt6 REQUIRED COMPONENTS Core Concurrent Gui Network Test Widgets)

# nxm:// URL parsing. Kept on its own because its integration sub-tests gate on
# an env var (see add_test below).
add_executable(test_nxm
    test_nxm.cpp
    ${CMAKE_SOURCE_DIR}/src/nxmurl.cpp
)
target_include_directories(test_nxm PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_nxm PRIVATE Qt6::Core Qt6::Network)

# openmw.cfg + launcher.cfg writers and sync-prep.
add_executable(test_openmw_config
    test_openmw_config.cpp
    ${CMAKE_SOURCE_DIR}/src/openmwconfigwriter.cpp
    ${CMAKE_SOURCE_DIR}/src/master_satisfaction.cpp
    ${CMAKE_SOURCE_DIR}/src/pluginparser.cpp
)
target_include_directories(test_openmw_config PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_openmw_config PRIVATE Qt6::Core)

# FOMOD/BAIN install path: path resolve, copy, scripts, install, BAIN, and the
# headless wizard UI driver (FomodWizard is a QDialog Q_OBJECT -> AUTOMOC +
# Widgets, and pulls translator.h -> generated translation_keys.h).
add_executable(test_fomod
    test_fomod.cpp
    ${CMAKE_SOURCE_DIR}/src/fomod_path.cpp
    ${CMAKE_SOURCE_DIR}/src/fomod_copy.cpp
    ${CMAKE_SOURCE_DIR}/src/fomod_scripts.cpp
    ${CMAKE_SOURCE_DIR}/src/fomod_install.cpp
    ${CMAKE_SOURCE_DIR}/src/bain.cpp
    ${CMAKE_SOURCE_DIR}/src/fomodwizard.cpp
    ${CMAKE_SOURCE_DIR}/src/translator.cpp
    ${CMAKE_SOURCE_DIR}/include/fomodwizard.h
)
set_target_properties(test_fomod PROPERTIES AUTOMOC ON)
target_include_directories(test_fomod PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(test_fomod PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

# Bethesda subsystem: deploy, load order, archive ini, proton paths, adapters.
add_executable(test_bethesda
    test_bethesda.cpp
    ${CMAKE_SOURCE_DIR}/src/deployment_report.cpp
    ${CMAKE_SOURCE_DIR}/src/bethesda_deploy.cpp
    ${CMAKE_SOURCE_DIR}/src/bethesda_loadorder.cpp
    ${CMAKE_SOURCE_DIR}/src/bethesda_archives.cpp
    ${CMAKE_SOURCE_DIR}/src/proton_paths.cpp
    ${CMAKE_SOURCE_DIR}/src/game_adapters.cpp
    ${CMAKE_SOURCE_DIR}/include/game_adapter.h
)
target_include_directories(test_bethesda PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_bethesda PRIVATE Qt6::Core)

# Modlist persistence: io, serializer, ModEntry value type, sync guard.
add_executable(test_modlist
    test_modlist.cpp
    ${CMAKE_SOURCE_DIR}/src/modlist_io.cpp
    ${CMAKE_SOURCE_DIR}/src/safe_fs.cpp
    ${CMAKE_SOURCE_DIR}/src/modlist_serializer.cpp
    ${CMAKE_SOURCE_DIR}/src/modentry.cpp
    ${CMAKE_SOURCE_DIR}/src/modlist_sync_guard.cpp
)
target_include_directories(test_modlist PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_modlist PRIVATE Qt6::Core Qt6::Gui)

# ModlistModel (Q_OBJECT, QSignalSpy) + game/modlist profiles (Q_OBJECT).
add_executable(test_profiles
    test_profiles.cpp
    ${CMAKE_SOURCE_DIR}/src/modlist_model.cpp
    ${CMAKE_SOURCE_DIR}/src/modentry.cpp
    ${CMAKE_SOURCE_DIR}/src/nxmurl.cpp
    ${CMAKE_SOURCE_DIR}/src/game_profiles.cpp
    ${CMAKE_SOURCE_DIR}/src/game_adapters.cpp
    ${CMAKE_SOURCE_DIR}/src/settings.cpp
    ${CMAKE_SOURCE_DIR}/include/modlist_model.h
    ${CMAKE_SOURCE_DIR}/include/game_profiles.h
)
set_target_properties(test_profiles PROPERTIES AUTOMOC ON)
target_include_directories(test_profiles PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_profiles PRIVATE Qt6::Core Qt6::Gui Qt6::Test)

# Nexus API parsing + dependency resolution.
add_executable(test_nexus
    test_nexus.cpp
    ${CMAKE_SOURCE_DIR}/src/nexusclient.cpp
    ${CMAKE_SOURCE_DIR}/src/deps_resolver.cpp
)
target_include_directories(test_nexus PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_nexus PRIVATE Qt6::Core Qt6::Network)

# Plugin/BSA parsing, master satisfaction, plugin + asset collisions.
add_executable(test_plugins
    test_plugins.cpp
    ${CMAKE_SOURCE_DIR}/src/pluginparser.cpp
    ${CMAKE_SOURCE_DIR}/src/bsareader.cpp
    ${CMAKE_SOURCE_DIR}/src/master_satisfaction.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin_collisions.cpp
    ${CMAKE_SOURCE_DIR}/src/asset_collisions.cpp
)
target_include_directories(test_plugins PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_plugins PRIVATE Qt6::Core)

# Load-order merge + ScanCoordinator (Q_OBJECT, takes a QListWidget*).
add_executable(test_load_order
    test_load_order.cpp
    ${CMAKE_SOURCE_DIR}/src/load_order_merge.cpp
    ${CMAKE_SOURCE_DIR}/src/scan_coordinator.cpp
    ${CMAKE_SOURCE_DIR}/src/pluginparser.cpp
    ${CMAKE_SOURCE_DIR}/include/scan_coordinator.h
)
set_target_properties(test_load_order PROPERTIES AUTOMOC ON)
target_include_directories(test_load_order PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_load_order PRIVATE
    Qt6::Core Qt6::Concurrent Qt6::Gui Qt6::Widgets)

# Filesystem helpers: safe_fs + fs_utils (header-only).
add_executable(test_fs
    test_fs.cpp
    ${CMAKE_SOURCE_DIR}/src/safe_fs.cpp
)
target_include_directories(test_fs PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_fs PRIVATE Qt6::Core)

# BackupManager checkpoints, incl. the auto pre-sort good-state (Q_OBJECT ->
# AUTOMOC; pulls prompts + translator.h -> generated translation_keys.h).
add_executable(test_backup
    test_backup.cpp
    ${CMAKE_SOURCE_DIR}/src/backup_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/safe_fs.cpp
    ${CMAKE_SOURCE_DIR}/src/prompts.cpp
    ${CMAKE_SOURCE_DIR}/src/translator.cpp
    ${CMAKE_SOURCE_DIR}/include/backup_manager.h
)
set_target_properties(test_backup PROPERTIES AUTOMOC ON)
target_include_directories(test_backup PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(test_backup PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

# Settings migrations + per-game forbidden mods (registry TU is a Q_OBJECT and
# pulls translator.h -> generated translation_keys.h).
add_executable(test_settings
    test_settings.cpp
    ${CMAKE_SOURCE_DIR}/src/settings_migrations.cpp
    ${CMAKE_SOURCE_DIR}/src/forbidden_mods.cpp
    ${CMAKE_SOURCE_DIR}/src/settings.cpp
    ${CMAKE_SOURCE_DIR}/src/translator.cpp
    ${CMAKE_SOURCE_DIR}/include/forbidden_mods.h
)
set_target_properties(test_settings PROPERTIES AUTOMOC ON)
target_include_directories(test_settings PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(test_settings PRIVATE Qt6::Core Qt6::Widgets)

# Mod naming, sharing, install layout, post-install heuristics, modlist summary.
add_executable(test_mods
    test_mods.cpp
    ${CMAKE_SOURCE_DIR}/src/mod_naming.cpp
    ${CMAKE_SOURCE_DIR}/src/mod_sharing.cpp
    ${CMAKE_SOURCE_DIR}/src/modlist_serializer.cpp
    ${CMAKE_SOURCE_DIR}/src/modlist_summary.cpp
    ${CMAKE_SOURCE_DIR}/src/modentry.cpp
    ${CMAKE_SOURCE_DIR}/src/install_layout.cpp
    ${CMAKE_SOURCE_DIR}/src/post_install.cpp
)
target_include_directories(test_mods PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_mods PRIVATE Qt6::Core Qt6::Gui)

# Archive magic sniffing + extractor error-message routing (both pure, moc-free).
add_executable(test_extract
    test_extract.cpp
    ${CMAKE_SOURCE_DIR}/src/archive_magic.cpp
    ${CMAKE_SOURCE_DIR}/src/extract_errors.cpp
)
target_include_directories(test_extract PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_extract PRIVATE Qt6::Core)

# CTest registration. "unit" = hermetic; test_nxm also carries an "integration"
# variant gated on NEREVARINE_SKIP_INTEGRATION.
set(UNIT_TESTS
    test_openmw_config
    test_fomod
    test_bethesda
    test_modlist
    test_profiles
    test_nexus
    test_plugins
    test_load_order
    test_fs
    test_backup
    test_settings
    test_mods
    test_extract
)

foreach(t ${UNIT_TESTS})
    add_test(NAME ${t} COMMAND ${t})
    set_tests_properties(${t} PROPERTIES LABELS "unit")
endforeach()

add_test(NAME test_nxm COMMAND test_nxm)
set_tests_properties(test_nxm PROPERTIES
    LABELS "unit"
    ENVIRONMENT "NEREVARINE_SKIP_INTEGRATION=1")

add_test(NAME test_nxm_integration COMMAND test_nxm)
set_tests_properties(test_nxm_integration PROPERTIES LABELS "integration")
