find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Test)

add_executable(test_nxm
    test_nxm.cpp
    ${CMAKE_SOURCE_DIR}/src/nxmurl.cpp
)
target_include_directories(test_nxm PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_nxm PRIVATE
    Qt6::Core
    Qt6::Network
)

add_executable(test_annotation_codec test_annotation_codec.cpp)
target_include_directories(test_annotation_codec PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_annotation_codec PRIVATE
    Qt6::Core
)

add_executable(test_plugin_parser
    test_plugin_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/pluginparser.cpp
)
target_include_directories(test_plugin_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_plugin_parser PRIVATE
    Qt6::Core
)

add_executable(test_fomod_path
    test_fomod_path.cpp
    ${CMAKE_SOURCE_DIR}/src/fomod_path.cpp
)
target_include_directories(test_fomod_path PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_fomod_path PRIVATE
    Qt6::Core
)

add_executable(test_fomod_install
    test_fomod_install.cpp
    ${CMAKE_SOURCE_DIR}/src/fomod_install.cpp
)
target_include_directories(test_fomod_install PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_fomod_install PRIVATE
    Qt6::Core
)

add_executable(test_modlist_model
    test_modlist_model.cpp
    ${CMAKE_SOURCE_DIR}/src/modlist_model.cpp
    ${CMAKE_SOURCE_DIR}/src/modentry.cpp
    ${CMAKE_SOURCE_DIR}/include/modlist_model.h
)
# ModlistModel is a Q_OBJECT - needs AUTOMOC for the vtable.
set_target_properties(test_modlist_model PROPERTIES AUTOMOC ON)
target_include_directories(test_modlist_model PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_modlist_model PRIVATE
    Qt6::Core
    Qt6::Gui     # ModEntry has QColor members → QtGui (header-only here)
    Qt6::Test    # QSignalSpy
)

add_executable(test_modlist_profiles
    test_modlist_profiles.cpp
    ${CMAKE_SOURCE_DIR}/src/game_profiles.cpp
    ${CMAKE_SOURCE_DIR}/include/game_profiles.h
)
# GameProfileRegistry is a Q_OBJECT - needs AUTOMOC for the vtable.
# Listing the header in the source list makes AUTOMOC pick it up reliably,
# even when the target's working dir is tests/ rather than the header's dir.
set_target_properties(test_modlist_profiles PROPERTIES AUTOMOC ON)
target_include_directories(test_modlist_profiles PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_modlist_profiles PRIVATE
    Qt6::Core
)

add_executable(test_fomod_copy
    test_fomod_copy.cpp
    ${CMAKE_SOURCE_DIR}/src/fomod_copy.cpp
)
target_include_directories(test_fomod_copy PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_fomod_copy PRIVATE
    Qt6::Core
)

add_executable(test_fomod_scripts
    test_fomod_scripts.cpp
    ${CMAKE_SOURCE_DIR}/src/fomod_scripts.cpp
    ${CMAKE_SOURCE_DIR}/src/fomod_path.cpp
)
target_include_directories(test_fomod_scripts PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_fomod_scripts PRIVATE
    Qt6::Core
)

add_executable(test_install_layout
    test_install_layout.cpp
    ${CMAKE_SOURCE_DIR}/src/install_layout.cpp
)
target_include_directories(test_install_layout PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_install_layout PRIVATE
    Qt6::Core
)

add_executable(test_load_order_merge
    test_load_order_merge.cpp
    ${CMAKE_SOURCE_DIR}/src/load_order_merge.cpp
)
target_include_directories(test_load_order_merge PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_load_order_merge PRIVATE
    Qt6::Core
)

add_executable(test_fs_utils test_fs_utils.cpp)
target_include_directories(test_fs_utils PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_fs_utils PRIVATE
    Qt6::Core
)

add_executable(test_openmw_config_writer
    test_openmw_config_writer.cpp
    ${CMAKE_SOURCE_DIR}/src/openmwconfigwriter.cpp
)
target_include_directories(test_openmw_config_writer PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_openmw_config_writer PRIVATE
    Qt6::Core
)

add_executable(test_openmw_config_writer_fuzz
    test_openmw_config_writer_fuzz.cpp
    ${CMAKE_SOURCE_DIR}/src/openmwconfigwriter.cpp
)
target_include_directories(test_openmw_config_writer_fuzz PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_openmw_config_writer_fuzz PRIVATE
    Qt6::Core
)

add_executable(test_launcher_cfg_writer
    test_launcher_cfg_writer.cpp
    ${CMAKE_SOURCE_DIR}/src/openmwconfigwriter.cpp
)
target_include_directories(test_launcher_cfg_writer PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_launcher_cfg_writer PRIVATE
    Qt6::Core
)

add_executable(test_log_triage
    test_log_triage.cpp
    ${CMAKE_SOURCE_DIR}/src/log_triage.cpp
)
target_include_directories(test_log_triage PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_log_triage PRIVATE
    Qt6::Core
)

add_executable(test_plugin_collisions
    test_plugin_collisions.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin_collisions.cpp
)
target_include_directories(test_plugin_collisions PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_plugin_collisions PRIVATE
    Qt6::Core
)

add_executable(test_asset_collisions
    test_asset_collisions.cpp
    ${CMAKE_SOURCE_DIR}/src/asset_collisions.cpp
)
target_include_directories(test_asset_collisions PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_asset_collisions PRIVATE
    Qt6::Core
)

add_executable(test_modlist_sync_guard
    test_modlist_sync_guard.cpp
    ${CMAKE_SOURCE_DIR}/src/modlist_sync_guard.cpp
)
target_include_directories(test_modlist_sync_guard PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_modlist_sync_guard PRIVATE
    Qt6::Core
)

add_executable(test_settings_migrations
    test_settings_migrations.cpp
    ${CMAKE_SOURCE_DIR}/src/settings_migrations.cpp
)
target_include_directories(test_settings_migrations PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_settings_migrations PRIVATE
    Qt6::Core
)

add_executable(test_master_satisfaction
    test_master_satisfaction.cpp
    ${CMAKE_SOURCE_DIR}/src/master_satisfaction.cpp
    ${CMAKE_SOURCE_DIR}/src/pluginparser.cpp
)
target_include_directories(test_master_satisfaction PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_master_satisfaction PRIVATE
    Qt6::Core
)

add_executable(test_bsa_reader
    test_bsa_reader.cpp
    ${CMAKE_SOURCE_DIR}/src/bsareader.cpp
)
target_include_directories(test_bsa_reader PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_bsa_reader PRIVATE
    Qt6::Core
)

add_executable(test_safe_fs
    test_safe_fs.cpp
    ${CMAKE_SOURCE_DIR}/src/safe_fs.cpp
)
target_include_directories(test_safe_fs PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_safe_fs PRIVATE
    Qt6::Core
)

add_executable(test_deps_resolver
    test_deps_resolver.cpp
    ${CMAKE_SOURCE_DIR}/src/deps_resolver.cpp
)
target_include_directories(test_deps_resolver PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_deps_resolver PRIVATE
    Qt6::Core
)

add_executable(test_nexus_client
    test_nexus_client.cpp
    ${CMAKE_SOURCE_DIR}/src/nexusclient.cpp
)
target_include_directories(test_nexus_client PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_nexus_client PRIVATE
    Qt6::Core
    Qt6::Network
)

add_executable(test_modlist_serialization
    test_modlist_serialization.cpp
    ${CMAKE_SOURCE_DIR}/src/modlist_serializer.cpp
    ${CMAKE_SOURCE_DIR}/src/modentry.cpp
)
target_include_directories(test_modlist_serialization PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_modlist_serialization PRIVATE
    Qt6::Core
    Qt6::Gui    # ModEntry has QColor members → QtGui (header-only here)
)

# Pure value-type tests for ModEntry.  Links ONLY the QtCore half
# (modentry.cpp), NOT modentry_item.cpp - that's the whole point of the
# split: business logic is testable without QtWidgets on the link line.
add_executable(test_modentry
    test_modentry.cpp
    ${CMAKE_SOURCE_DIR}/src/modentry.cpp
)
target_include_directories(test_modentry PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_modentry PRIVATE
    Qt6::Core
    Qt6::Gui       # QColor lives in QtGui (header-only use here)
)

# -- CTest registration ---
#
# Two labels: "unit" (hermetic: pure functions + QTemporaryDir, no network, no
# installed system files) and "integration" (test_nxm's KIO-protocol-file +
# Nexus-API-reachability sub-tests).  CI runs `ctest -L unit`; local
# developers can run `ctest` to exercise everything.
#
# test_nxm gates its integration sub-tests on NEREVARINE_SKIP_INTEGRATION=1
# so the single executable can serve both labels without splitting the suite.

set(UNIT_TESTS
    test_annotation_codec
    test_asset_collisions
    test_bsa_reader
    test_deps_resolver
    test_fomod_copy
    test_fomod_install
    test_fomod_path
    test_fomod_scripts
    test_install_layout
    test_fs_utils
    test_launcher_cfg_writer
    test_load_order_merge
    test_log_triage
    test_master_satisfaction
    test_modentry
    test_modlist_model
    test_modlist_profiles
    test_modlist_serialization
    test_modlist_sync_guard
    test_nexus_client
    test_openmw_config_writer
    test_openmw_config_writer_fuzz
    test_plugin_collisions
    test_plugin_parser
    test_safe_fs
    test_settings_migrations
)

foreach(t ${UNIT_TESTS})
    add_test(NAME ${t} COMMAND ${t})
    set_tests_properties(${t} PROPERTIES LABELS "unit")
endforeach()

# test_nxm: unit-mode entry skips the integration sub-tests via env var.
add_test(NAME test_nxm COMMAND test_nxm)
set_tests_properties(test_nxm PROPERTIES
    LABELS "unit"
    ENVIRONMENT "NEREVARINE_SKIP_INTEGRATION=1")

# Full test_nxm (includes KIO protocol file + live Nexus API check).  Only
# meaningful on a developer workstation that has installed the protocol
# handlers and has internet.  `ctest -L integration` runs these.
add_test(NAME test_nxm_integration COMMAND test_nxm)
set_tests_properties(test_nxm_integration PROPERTIES LABELS "integration")
