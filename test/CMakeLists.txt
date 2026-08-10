add_executable(oxq_core_header_smoke core/header_smoke.cpp)
target_link_libraries(oxq_core_header_smoke PRIVATE OXQF::core)
oxqf_set_project_warnings(oxq_core_header_smoke)
oxqf_enable_sanitizers(oxq_core_header_smoke)
add_test(NAME core.header-smoke COMMAND oxq_core_header_smoke)

add_executable(oxq_core_game_model_test core/game_model_test.cpp)
target_link_libraries(oxq_core_game_model_test PRIVATE OXQF::core)
oxqf_set_project_warnings(oxq_core_game_model_test)
oxqf_enable_sanitizers(oxq_core_game_model_test)
add_test(NAME core.game-model COMMAND oxq_core_game_model_test)

add_executable(oxq_core_codec_container_test core/codec/container_test.cpp)
target_link_libraries(oxq_core_codec_container_test PRIVATE OXQF::core)
target_include_directories(
  oxq_core_codec_container_test
  PRIVATE "${PROJECT_SOURCE_DIR}/src/core/src"
)
target_compile_definitions(
  oxq_core_codec_container_test
  PRIVATE OXQF_VECTOR_DIRECTORY="${PROJECT_SOURCE_DIR}/test/vectors/oxq-v1"
)
oxqf_set_project_warnings(oxq_core_codec_container_test)
oxqf_enable_sanitizers(oxq_core_codec_container_test)
add_test(NAME core.codec-container COMMAND oxq_core_codec_container_test)

add_executable(oxq_convert_header_smoke convert/header_smoke.cpp)
target_link_libraries(oxq_convert_header_smoke PRIVATE OXQF::convert)
oxqf_set_project_warnings(oxq_convert_header_smoke)
oxqf_enable_sanitizers(oxq_convert_header_smoke)
add_test(NAME convert.header-smoke COMMAND oxq_convert_header_smoke)

add_test(NAME cli.version COMMAND oxq --version)
set_tests_properties(cli.version PROPERTIES PASS_REGULAR_EXPRESSION "^oxq ${PROJECT_VERSION}")

if(OXQF_ENABLE_SANITIZERS AND NOT MSVC)
  set_tests_properties(
    core.header-smoke
    core.game-model
    core.codec-container
    convert.header-smoke
    cli.version
    PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
  )
endif()

find_program(OXQF_NODE_EXECUTABLE NAMES node REQUIRED)
add_test(
  NAME tooling.node-environment
  COMMAND "${OXQF_NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/script/check-environment.mjs"
)

add_test(
  NAME package.consumer-smoke
  COMMAND
    "${CMAKE_COMMAND}"
    "-DOXQF_BUILD_DIR=${PROJECT_BINARY_DIR}"
    "-DOXQF_SOURCE_DIR=${PROJECT_SOURCE_DIR}/test/consumer"
    "-DOXQF_GENERATOR=${CMAKE_GENERATOR}"
    "-DOXQF_CONFIG=$<CONFIG>"
    -P "${PROJECT_SOURCE_DIR}/test/consumer/run.cmake"
)
