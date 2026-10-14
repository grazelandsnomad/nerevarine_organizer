# One executable per suite, each a single source file linked against
# nerevarine_core (top-level CMakeLists): the library carries the include
# dirs, the Qt modules and the generated translation header, so a test lists
# nothing but itself. No test defines a Q_OBJECT class, so moc has nothing to
# do here.
set(CMAKE_AUTOMOC OFF)

find_package(Qt6 REQUIRED COMPONENTS Test)   # QSignalSpy, in test_profiles

function(nrv_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE nerevarine_core)
    nrv_warnings(${name})
endfunction()

# Hermetic suites: what CI runs, as `ctest -L unit`.
#   test_openmw_config          openmw.cfg + launcher.cfg writers, sync-prep
#   test_fomod                  FOMOD/BAIN install path and the headless wizard
#                               driver (a real QApplication, offscreen)
#   test_bethesda               Bethesda deploy, load order, archive ini, proton
#                               paths, game adapters
#   test_opengothic             Gothic II detection, VDFS stamps, mod inis
#   test_store_scan             Heroic/GOG/Steam detection, title normalisation
#   test_skse                   script-extender plugins against the installed game
#   test_download_watch         catching a manual download
#   test_modlist                modlist io, serializer, ModEntry, sync guard
#   test_profiles               ModlistModel signals, game + modlist profiles
#   test_translate_ui           the translate editor's table behaviour
#   test_nexus                  Nexus API parsing, dependency resolution
#   test_plugins                plugin/BSA parsing, masters, collisions
#   test_load_order             load-order merge, ScanCoordinator, undo
#   test_fs                     safe_fs + fs_utils
#   test_user_state_paths       where a state file lives, and the one-time move
#                               of a file an older layout left one dir up
#   test_async                  async::guarded (a real thread pool + event loop)
#   test_backup                 BackupManager checkpoints
#   test_settings               settings migrations, forbidden mods
#   test_mods                   naming, sharing, install layout, post-install,
#                               summary, placeholder state, row captions
#   test_extract                archive magic, extractor errors, InstallController
#   test_loadorder_controller   the scan workers' lifecycle
#   test_translation_coverage   the "no translation" verdict
#   test_launch_warnings        the launch-blocking rules
#   test_queues                 the install queues and rows that vanish mid-flight
#   test_modlist_views          the model bridge, the dependency snapshot, the sort orders
#   test_nxm                    nxm:// URL parsing; its networked sub-tests are
#                               the integration variant registered below
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
    test_user_state_paths
    test_async
    test_backup
    test_settings
    test_mods
    test_extract
    test_loadorder_controller
    test_translation_coverage
    test_launch_warnings
    test_queues
    test_modlist_views
    test_nxm
)
foreach(t IN LISTS UNIT_TESTS)
    nrv_test(${t})
    add_test(NAME ${t} COMMAND ${t})
    set_tests_properties(${t} PROPERTIES LABELS "unit")
endforeach()
target_link_libraries(test_profiles PRIVATE Qt6::Test)
target_link_libraries(test_modlist_views PRIVATE Qt6::Test)   # QAbstractItemModelTester
set_tests_properties(test_nxm PROPERTIES
    ENVIRONMENT "NEREVARINE_SKIP_INTEGRATION=1")

# Not hermetic, so not under the unit label. 77 = "skipped", the automake
# convention: the binary returns it when its input is not there (no corpus,
# no network), so a machine without it reports a skip instead of a failure.

# Real FOMOD installers replayed against a recorded baseline. The corpus is
# other people's installers, kept outside the repo and named by
# NRV_FOMOD_CORPUS; unset, the binary skips.
nrv_test(test_fomod_corpus)
add_test(NAME test_fomod_corpus COMMAND test_fomod_corpus)
set_tests_properties(test_fomod_corpus PROPERTIES LABELS "corpus"
    SKIP_RETURN_CODE 77)

# The v2 GraphQL queries against the live endpoint - a canary for silent
# truncation or schema drift, which every caller would otherwise absorb by
# degrading quietly. Run daily by sanitizers.yml.
nrv_test(test_nexus_v2_live)
add_test(NAME test_nexus_v2_live COMMAND test_nexus_v2_live)
set_tests_properties(test_nexus_v2_live PROPERTIES LABELS "integration"
    SKIP_RETURN_CODE 77)

# test_nxm again, this time with its networked sub-tests on.
add_test(NAME test_nxm_integration COMMAND test_nxm)
set_tests_properties(test_nxm_integration PROPERTIES LABELS "integration"
    SKIP_RETURN_CODE 77)
