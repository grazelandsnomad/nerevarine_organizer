find_package(Qt6 REQUIRED COMPONENTS Core Concurrent Gui Network Test Widgets)

# nxm:// URL parsing. Kept on its own because its integration sub-tests gate on
# an env var (see add_test below).
add_executable(test_nxm
    test_nxm.cpp
    ${CMAKE_SOURCE_DIR}/src/nxmurl.cpp
    ${CMAKE_SOURCE_DIR}/src/game_match.cpp
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
    ${CMAKE_SOURCE_DIR}/src/fomod_hint.cpp
    ${CMAKE_SOURCE_DIR}/src/mod_aliases.cpp
    ${CMAKE_SOURCE_DIR}/src/mod_match.cpp
    ${CMAKE_SOURCE_DIR}/src/bain_hint.cpp
    ${CMAKE_SOURCE_DIR}/src/pluginparser.cpp
    ${CMAKE_SOURCE_DIR}/src/nxmurl.cpp
    ${CMAKE_SOURCE_DIR}/src/fomod_scripts.cpp
    ${CMAKE_SOURCE_DIR}/src/fomod_install.cpp
    ${CMAKE_SOURCE_DIR}/src/bain.cpp
    ${CMAKE_SOURCE_DIR}/src/bainwizard.cpp
    ${CMAKE_SOURCE_DIR}/src/fomodwizard.cpp
    ${CMAKE_SOURCE_DIR}/src/translator.cpp
    ${CMAKE_SOURCE_DIR}/include/fomodwizard.h
    ${CMAKE_SOURCE_DIR}/include/bainwizard.h
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
    ${CMAKE_SOURCE_DIR}/src/bethesda_custom_ini.cpp
    ${CMAKE_SOURCE_DIR}/src/proton_paths.cpp
    ${CMAKE_SOURCE_DIR}/src/dll_overrides.cpp
    ${CMAKE_SOURCE_DIR}/src/game_adapters.cpp
    ${CMAKE_SOURCE_DIR}/include/game_adapter.h
)
target_include_directories(test_bethesda PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_bethesda PRIVATE Qt6::Core)

# OpenGothic (Gothic II): install detection, VDFS archive stamps, mod inis.
# game_adapters.cpp comes along so the Gothic profile's own wiring is checked
# against the same registry the app uses.
add_executable(test_opengothic
    test_opengothic.cpp
    ${CMAKE_SOURCE_DIR}/src/opengothic.cpp
    ${CMAKE_SOURCE_DIR}/src/fomod_path.cpp
    ${CMAKE_SOURCE_DIR}/src/game_adapters.cpp
    ${CMAKE_SOURCE_DIR}/include/game_adapter.h
)
target_include_directories(test_opengothic PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_opengothic PRIVATE Qt6::Core)

# Store detection: Heroic/GOG caches, Steam appmanifest, title normalisation.
# game_adapters.cpp comes along so the shipped folder candidates are what the
# match is tested against, not a copy of them.
# Script-extender plugins against the installed game: PE version data, the
# address library's format header, and the verdict over both.
add_executable(test_skse
    test_skse.cpp
    ${CMAKE_SOURCE_DIR}/src/pe_info.cpp
    ${CMAKE_SOURCE_DIR}/src/skse_check.cpp
)
target_include_directories(test_skse PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_skse PRIVATE Qt6::Core)

add_executable(test_store_scan
    test_store_scan.cpp
    ${CMAKE_SOURCE_DIR}/src/store_scan.cpp
    ${CMAKE_SOURCE_DIR}/src/game_store.cpp
    ${CMAKE_SOURCE_DIR}/src/game_adapters.cpp
    ${CMAKE_SOURCE_DIR}/include/game_adapter.h
)
target_include_directories(test_store_scan PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_store_scan PRIVATE Qt6::Core)

# Catching a manual download: partial-name detection, mod id recovery, settle.
# Q_OBJECT in the watcher, so AUTOMOC.
add_executable(test_download_watch
    test_download_watch.cpp
    ${CMAKE_SOURCE_DIR}/src/download_watch.cpp
    ${CMAKE_SOURCE_DIR}/include/download_watch.h
)
set_target_properties(test_download_watch PROPERTIES AUTOMOC ON)
target_include_directories(test_download_watch PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_download_watch PRIVATE Qt6::Core)

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
    ${CMAKE_SOURCE_DIR}/src/game_store.cpp
    ${CMAKE_SOURCE_DIR}/src/game_adapters.cpp
    ${CMAKE_SOURCE_DIR}/src/store_scan.cpp
    ${CMAKE_SOURCE_DIR}/src/settings.cpp
    ${CMAKE_SOURCE_DIR}/src/target_language.cpp
    ${CMAKE_SOURCE_DIR}/src/language_guess.cpp
    ${CMAKE_SOURCE_DIR}/src/lore_overrides.cpp
    ${CMAKE_SOURCE_DIR}/src/mod_package.cpp
    ${CMAKE_SOURCE_DIR}/src/term_protect.cpp
    ${CMAKE_SOURCE_DIR}/src/translation_rules.cpp
    ${CMAKE_SOURCE_DIR}/src/subprocess.cpp
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
    ${CMAKE_SOURCE_DIR}/src/nxmurl.cpp
    ${CMAKE_SOURCE_DIR}/src/file_pick.cpp
    ${CMAKE_SOURCE_DIR}/src/game_store.cpp
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
    ${CMAKE_SOURCE_DIR}/src/log_triage.cpp
)
target_include_directories(test_plugins PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_plugins PRIVATE Qt6::Core)

# Load-order merge + ScanCoordinator (Q_OBJECT, takes a QListWidget*).
add_executable(test_load_order
    test_load_order.cpp
    ${CMAKE_SOURCE_DIR}/src/load_order_merge.cpp
    ${CMAKE_SOURCE_DIR}/src/scan_coordinator.cpp
    ${CMAKE_SOURCE_DIR}/src/conflict_direction.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin_records.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin_strings.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin_writer.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin_text.cpp
    ${CMAKE_SOURCE_DIR}/src/target_language.cpp
    ${CMAKE_SOURCE_DIR}/src/language_guess.cpp
    ${CMAKE_SOURCE_DIR}/src/lore_overrides.cpp
    ${CMAKE_SOURCE_DIR}/src/mod_package.cpp
    ${CMAKE_SOURCE_DIR}/src/term_protect.cpp
    ${CMAKE_SOURCE_DIR}/src/translation_rules.cpp
    ${CMAKE_SOURCE_DIR}/src/subprocess.cpp
    ${CMAKE_SOURCE_DIR}/src/google_translate.cpp
    ${CMAKE_SOURCE_DIR}/src/translation_store.cpp
    ${CMAKE_SOURCE_DIR}/src/translation_mod.cpp
    ${CMAKE_SOURCE_DIR}/src/pluginparser.cpp
    ${CMAKE_SOURCE_DIR}/src/modentry.cpp
    ${CMAKE_SOURCE_DIR}/src/modentry_item.cpp
    ${CMAKE_SOURCE_DIR}/src/undo_stack.cpp
    ${CMAKE_SOURCE_DIR}/include/scan_coordinator.h
    ${CMAKE_SOURCE_DIR}/include/undo_stack.h
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

# async::guarded helper (header-only; needs a real thread pool + event loop).
add_executable(test_async
    test_async.cpp
)
target_include_directories(test_async PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_async PRIVATE Qt6::Core Qt6::Concurrent)

# BackupManager checkpoints, incl. the auto pre-sort good-state (Q_OBJECT ->
# AUTOMOC; pulls prompts + translator.h -> generated translation_keys.h).
add_executable(test_backup
    test_backup.cpp
    ${CMAKE_SOURCE_DIR}/src/backup_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/backup_ops.cpp
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
    ${CMAKE_SOURCE_DIR}/src/mod_cleanup.cpp
    ${CMAKE_SOURCE_DIR}/src/mod_sharing.cpp
    ${CMAKE_SOURCE_DIR}/src/modlist_serializer.cpp
    ${CMAKE_SOURCE_DIR}/src/modlist_summary.cpp
    ${CMAKE_SOURCE_DIR}/src/modentry.cpp
    ${CMAKE_SOURCE_DIR}/src/install_layout.cpp
    ${CMAKE_SOURCE_DIR}/src/post_install.cpp
    ${CMAKE_SOURCE_DIR}/src/variant_picker.cpp
    ${CMAKE_SOURCE_DIR}/src/fomod_copy.cpp
    ${CMAKE_SOURCE_DIR}/src/fomod_path.cpp
)
target_include_directories(test_mods PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_mods PRIVATE Qt6::Core Qt6::Gui)

# Archive magic sniffing + extractor error-message routing (both pure, moc-free).
add_executable(test_extract
    test_extract.cpp
    ${CMAKE_SOURCE_DIR}/src/archive_magic.cpp
    ${CMAKE_SOURCE_DIR}/src/extract_errors.cpp
    ${CMAKE_SOURCE_DIR}/src/installcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/install_layout.cpp
    ${CMAKE_SOURCE_DIR}/src/logging.cpp
    # Header listed so AUTOMOC mocs it (it lives in include/, not next to the
    # .cpp, so the same-directory header scan alone misses it).
    ${CMAKE_SOURCE_DIR}/include/installcontroller.h
)
target_include_directories(test_extract PRIVATE ${CMAKE_SOURCE_DIR}/include)
# Concurrent: InstallController's MD5 verify hashes on a QtConcurrent worker.
# AUTOMOC: InstallController is a Q_OBJECT (signals drive the cancel tests).
target_link_libraries(test_extract PRIVATE Qt6::Core Qt6::Concurrent)
set_target_properties(test_extract PROPERTIES AUTOMOC ON)


# Translate editor's table behaviour. A QDialog with Q_OBJECT -> AUTOMOC +
# Widgets, and translator.h pulls the generated translation_keys.h.
add_executable(test_translate_ui
    test_translate_ui.cpp
    ${CMAKE_SOURCE_DIR}/src/translate_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/markup_protect.cpp
    ${CMAKE_SOURCE_DIR}/src/translation_store.cpp
    ${CMAKE_SOURCE_DIR}/src/translation_progress.cpp
    ${CMAKE_SOURCE_DIR}/src/translation_mod.cpp
    ${CMAKE_SOURCE_DIR}/src/translation_rules.cpp
    ${CMAKE_SOURCE_DIR}/src/term_protect.cpp
    ${CMAKE_SOURCE_DIR}/src/google_translate.cpp
    ${CMAKE_SOURCE_DIR}/src/settings.cpp
    ${CMAKE_SOURCE_DIR}/src/lore_overrides.cpp
    ${CMAKE_SOURCE_DIR}/src/target_language.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin_writer.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin_strings.cpp
    ${CMAKE_SOURCE_DIR}/src/vanilla_text.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin_text.cpp
    ${CMAKE_SOURCE_DIR}/src/prompts.cpp
    ${CMAKE_SOURCE_DIR}/src/subprocess.cpp
    ${CMAKE_SOURCE_DIR}/src/translator.cpp
    ${CMAKE_SOURCE_DIR}/include/translate_dialog.h
)
set_target_properties(test_translate_ui PROPERTIES AUTOMOC ON)
target_include_directories(test_translate_ui PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(test_translate_ui PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)

# CTest registration. "unit" = hermetic; test_nxm also carries an "integration"
# variant gated on NEREVARINE_SKIP_INTEGRATION.
set(UNIT_TESTS
    test_openmw_config
    test_fomod
    test_bethesda
    test_opengothic
    test_store_scan
    test_skse
    test_download_watch
    test_modlist
    test_profiles
    test_translate_ui
    test_nexus
    test_plugins
    test_load_order
    test_fs
    test_async
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
