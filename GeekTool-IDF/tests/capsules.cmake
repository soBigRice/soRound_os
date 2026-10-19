# Capsule tests are local; no target opens serial ports or invokes a flasher.
# 胶囊测试仅本机运行，不打开串口或调用烧录器。
set(CAPSULE_TEST_ROOT "${CMAKE_CURRENT_LIST_DIR}")
add_executable(capsule_store_tests "${CAPSULE_TEST_ROOT}/capsule_store_tests.c")
target_include_directories(capsule_store_tests PRIVATE "${CAPSULE_TEST_ROOT}/../main")
target_compile_options(capsule_store_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME capsule_wav_storage_boundaries COMMAND capsule_store_tests)

add_executable(capsule_guard_tests "${CAPSULE_TEST_ROOT}/capsule_guard_tests.c")
target_include_directories(capsule_guard_tests PRIVATE "${CAPSULE_TEST_ROOT}/capsule_stubs" "${CAPSULE_TEST_ROOT}/../main")
target_compile_options(capsule_guard_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME capsule_storage_guard_no_implicit_initialization COMMAND capsule_guard_tests)

add_executable(capsule_audio_tests "${CAPSULE_TEST_ROOT}/capsule_audio_tests.c" "${CAPSULE_TEST_ROOT}/../main/merit_sound.c")
target_include_directories(capsule_audio_tests PRIVATE "${CAPSULE_TEST_ROOT}/lifestyle/audio_stubs" "${CAPSULE_TEST_ROOT}/host/stubs" "${CAPSULE_TEST_ROOT}/../main")
target_compile_options(capsule_audio_tests PRIVATE -Wall -Wextra -Werror -Wno-missing-field-initializers)
target_link_libraries(capsule_audio_tests PRIVATE m)
add_test(NAME capsule_shared_audio_priority COMMAND capsule_audio_tests)

add_executable(capsule_recorder_tests "${CAPSULE_TEST_ROOT}/capsule_recorder_tests.c")
target_include_directories(capsule_recorder_tests PRIVATE "${CAPSULE_TEST_ROOT}/lifestyle/audio_stubs" "${CAPSULE_TEST_ROOT}/host/stubs" "${CAPSULE_TEST_ROOT}/../main")
target_compile_options(capsule_recorder_tests PRIVATE -Wall -Wextra -Werror -Wno-missing-field-initializers)
add_test(NAME capsule_recorder_worker_lifecycle COMMAND capsule_recorder_tests)

add_executable(capsule_ui_tests "${CAPSULE_TEST_ROOT}/capsule_ui_tests.c" "${CAPSULE_TEST_ROOT}/../main/tools_ui.c"
    "${CAPSULE_TEST_ROOT}/../main/font_tools.c" "${CAPSULE_TEST_ROOT}/../main/font_companion.c")
target_include_directories(capsule_ui_tests PRIVATE "${CAPSULE_TEST_ROOT}/host" "${CAPSULE_TEST_ROOT}/../main")
target_compile_options(capsule_ui_tests PRIVATE -Wall -Wextra -Werror -Wno-missing-field-initializers)
target_link_libraries(capsule_ui_tests PRIVATE lvgl m)
add_test(NAME capsule_native_ui_interactions COMMAND capsule_ui_tests)
