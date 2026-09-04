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

add_executable(oxq_core_state_validation_test core/state_validation_test.cpp)
target_link_libraries(oxq_core_state_validation_test PRIVATE OXQF::core)
target_compile_definitions(
  oxq_core_state_validation_test
  PRIVATE OXQF_VECTOR_DIRECTORY="${PROJECT_SOURCE_DIR}/test/vectors/oxq-v1"
)
oxqf_set_project_warnings(oxq_core_state_validation_test)
oxqf_enable_sanitizers(oxq_core_state_validation_test)
add_test(NAME core.state-validation COMMAND oxq_core_state_validation_test)

add_executable(oxq_core_unicode_nfc_test core/unicode_nfc_test.cpp)
target_link_libraries(oxq_core_unicode_nfc_test PRIVATE OXQF::core)
target_include_directories(
  oxq_core_unicode_nfc_test
  PRIVATE "${PROJECT_SOURCE_DIR}/src/core/src"
)
oxqf_set_project_warnings(oxq_core_unicode_nfc_test)
oxqf_enable_sanitizers(oxq_core_unicode_nfc_test)
add_test(NAME core.unicode-nfc COMMAND oxq_core_unicode_nfc_test)

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

add_executable(oxq_core_codec_string_pool_test core/codec/string_pool_test.cpp)
target_link_libraries(oxq_core_codec_string_pool_test PRIVATE OXQF::core)
target_include_directories(
  oxq_core_codec_string_pool_test
  PRIVATE "${PROJECT_SOURCE_DIR}/src/core/src"
)
target_compile_definitions(
  oxq_core_codec_string_pool_test
  PRIVATE OXQF_VECTOR_DIRECTORY="${PROJECT_SOURCE_DIR}/test/vectors/oxq-v1"
)
oxqf_set_project_warnings(oxq_core_codec_string_pool_test)
oxqf_enable_sanitizers(oxq_core_codec_string_pool_test)
add_test(NAME core.codec-string-pool COMMAND oxq_core_codec_string_pool_test)

add_executable(oxq_core_codec_metadata_test core/codec/metadata_test.cpp)
target_link_libraries(oxq_core_codec_metadata_test PRIVATE OXQF::core)
target_include_directories(
  oxq_core_codec_metadata_test
  PRIVATE "${PROJECT_SOURCE_DIR}/src/core/src"
)
target_compile_definitions(
  oxq_core_codec_metadata_test
  PRIVATE OXQF_VECTOR_DIRECTORY="${PROJECT_SOURCE_DIR}/test/vectors/oxq-v1"
)
oxqf_set_project_warnings(oxq_core_codec_metadata_test)
oxqf_enable_sanitizers(oxq_core_codec_metadata_test)
add_test(NAME core.codec-metadata COMMAND oxq_core_codec_metadata_test)

add_executable(oxq_core_codec_restricted_json_test core/codec/restricted_json_test.cpp)
target_link_libraries(oxq_core_codec_restricted_json_test PRIVATE OXQF::core)
target_include_directories(
  oxq_core_codec_restricted_json_test
  PRIVATE "${PROJECT_SOURCE_DIR}/src/core/src"
)
oxqf_set_project_warnings(oxq_core_codec_restricted_json_test)
oxqf_enable_sanitizers(oxq_core_codec_restricted_json_test)
add_test(NAME core.codec-restricted-json COMMAND oxq_core_codec_restricted_json_test)

add_executable(oxq_core_codec_position_test core/codec/position_test.cpp)
target_link_libraries(oxq_core_codec_position_test PRIVATE OXQF::core)
target_include_directories(
  oxq_core_codec_position_test
  PRIVATE "${PROJECT_SOURCE_DIR}/src/core/src"
)
target_compile_definitions(
  oxq_core_codec_position_test
  PRIVATE OXQF_VECTOR_DIRECTORY="${PROJECT_SOURCE_DIR}/test/vectors/oxq-v1"
)
oxqf_set_project_warnings(oxq_core_codec_position_test)
oxqf_enable_sanitizers(oxq_core_codec_position_test)
add_test(NAME core.codec-position COMMAND oxq_core_codec_position_test)

add_executable(oxq_core_codec_annotation_test core/codec/annotation_test.cpp)
target_link_libraries(oxq_core_codec_annotation_test PRIVATE OXQF::core)
target_include_directories(
  oxq_core_codec_annotation_test
  PRIVATE "${PROJECT_SOURCE_DIR}/src/core/src"
)
target_compile_definitions(
  oxq_core_codec_annotation_test
  PRIVATE OXQF_VECTOR_DIRECTORY="${PROJECT_SOURCE_DIR}/test/vectors/oxq-v1"
)
oxqf_set_project_warnings(oxq_core_codec_annotation_test)
oxqf_enable_sanitizers(oxq_core_codec_annotation_test)
add_test(NAME core.codec-annotation COMMAND oxq_core_codec_annotation_test)

add_executable(oxq_core_codec_move_tree_test core/codec/move_tree_test.cpp)
target_link_libraries(oxq_core_codec_move_tree_test PRIVATE OXQF::core)
target_include_directories(
  oxq_core_codec_move_tree_test
  PRIVATE "${PROJECT_SOURCE_DIR}/src/core/src"
)
target_compile_definitions(
  oxq_core_codec_move_tree_test
  PRIVATE OXQF_VECTOR_DIRECTORY="${PROJECT_SOURCE_DIR}/test/vectors/oxq-v1"
)
oxqf_set_project_warnings(oxq_core_codec_move_tree_test)
oxqf_enable_sanitizers(oxq_core_codec_move_tree_test)
add_test(NAME core.codec-move-tree COMMAND oxq_core_codec_move_tree_test)

add_executable(oxq_core_reader_test core/reader_test.cpp)
target_link_libraries(oxq_core_reader_test PRIVATE OXQF::core)
target_compile_definitions(
  oxq_core_reader_test
  PRIVATE OXQF_VECTOR_DIRECTORY="${PROJECT_SOURCE_DIR}/test/vectors/oxq-v1"
)
oxqf_set_project_warnings(oxq_core_reader_test)
oxqf_enable_sanitizers(oxq_core_reader_test)
add_test(NAME core.reader COMMAND oxq_core_reader_test)

add_executable(oxq_core_validator_test core/validator_test.cpp)
target_link_libraries(oxq_core_validator_test PRIVATE OXQF::core)
target_compile_definitions(
  oxq_core_validator_test
  PRIVATE OXQF_VECTOR_DIRECTORY="${PROJECT_SOURCE_DIR}/test/vectors/oxq-v1"
)
oxqf_set_project_warnings(oxq_core_validator_test)
oxqf_enable_sanitizers(oxq_core_validator_test)
add_test(NAME core.validator COMMAND oxq_core_validator_test)

add_executable(oxq_core_writer_test core/writer_test.cpp)
target_link_libraries(oxq_core_writer_test PRIVATE OXQF::core)
target_compile_definitions(
  oxq_core_writer_test
  PRIVATE OXQF_VECTOR_DIRECTORY="${PROJECT_SOURCE_DIR}/test/vectors/oxq-v1"
)
oxqf_set_project_warnings(oxq_core_writer_test)
oxqf_enable_sanitizers(oxq_core_writer_test)
add_test(NAME core.writer COMMAND oxq_core_writer_test)

add_executable(oxq_core_malformed_regression_test core/malformed_regression_test.cpp)
target_link_libraries(oxq_core_malformed_regression_test PRIVATE OXQF::core)
target_compile_definitions(
  oxq_core_malformed_regression_test
  PRIVATE OXQF_VECTOR_DIRECTORY="${PROJECT_SOURCE_DIR}/test/vectors/oxq-v1"
)
oxqf_set_project_warnings(oxq_core_malformed_regression_test)
oxqf_enable_sanitizers(oxq_core_malformed_regression_test)
add_test(NAME core.malformed-regression COMMAND oxq_core_malformed_regression_test)

add_executable(oxq_core_deep_malformed_test core/deep_malformed_test.cpp)
target_link_libraries(oxq_core_deep_malformed_test PRIVATE OXQF::core)
target_include_directories(
  oxq_core_deep_malformed_test
  PRIVATE "${PROJECT_SOURCE_DIR}/src/core/src"
)
target_compile_definitions(
  oxq_core_deep_malformed_test
  PRIVATE OXQF_VECTOR_DIRECTORY="${PROJECT_SOURCE_DIR}/test/vectors/oxq-v1"
)
oxqf_set_project_warnings(oxq_core_deep_malformed_test)
oxqf_enable_sanitizers(oxq_core_deep_malformed_test)
add_test(NAME core.deep-malformed COMMAND oxq_core_deep_malformed_test)

if(OXQF_BUILD_FUZZERS)
  set(OXQF_READER_FUZZ_CORPUS "${CMAKE_CURRENT_BINARY_DIR}/reader-fuzz-corpus")
  set(OXQF_READER_FUZZ_ARTIFACTS "${CMAKE_CURRENT_BINARY_DIR}/reader-fuzz-artifacts")
  file(MAKE_DIRECTORY "${OXQF_READER_FUZZ_CORPUS}" "${OXQF_READER_FUZZ_ARTIFACTS}")
  file(
    COPY
      "${PROJECT_SOURCE_DIR}/test/vectors/oxq-v1/minimal.oxq"
      "${PROJECT_SOURCE_DIR}/test/vectors/oxq-v1/variation-zh.oxq"
      "${PROJECT_SOURCE_DIR}/test/vectors/oxq-v1/unknown-noncritical.oxq"
      "${PROJECT_SOURCE_DIR}/test/vectors/oxq-v1/invalid-magic-high-bit.oxq"
      "${PROJECT_SOURCE_DIR}/test/vectors/oxq-v1/invalid-magic-crlf.oxq"
      "${PROJECT_SOURCE_DIR}/test/vectors/oxq-v1/invalid-magic-eof.oxq"
    DESTINATION "${OXQF_READER_FUZZ_CORPUS}"
  )
  add_executable(oxq_core_reader_fuzz fuzz/reader_fuzz.cpp)
  target_link_libraries(oxq_core_reader_fuzz PRIVATE OXQF::core)
  target_compile_options(oxq_core_reader_fuzz PRIVATE -fsanitize=fuzzer)
  target_link_options(oxq_core_reader_fuzz PRIVATE -fsanitize=fuzzer)
  oxqf_set_project_warnings(oxq_core_reader_fuzz)
  add_test(
    NAME core.reader-fuzz-smoke
    COMMAND
      oxq_core_reader_fuzz
      -max_total_time=5
      -timeout=2
      "-artifact_prefix=${OXQF_READER_FUZZ_ARTIFACTS}/"
      "${OXQF_READER_FUZZ_CORPUS}"
  )
  set_tests_properties(
    core.reader-fuzz-smoke
    PROPERTIES
      ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
      TIMEOUT 15
  )
endif()

add_executable(oxq_convert_header_smoke convert/header_smoke.cpp)
target_link_libraries(oxq_convert_header_smoke PRIVATE OXQF::convert)
oxqf_set_project_warnings(oxq_convert_header_smoke)
oxqf_enable_sanitizers(oxq_convert_header_smoke)
add_test(NAME convert.header-smoke COMMAND oxq_convert_header_smoke)

add_executable(oxq_convert_cbl_container_test convert/cbl_container_test.cpp)
target_link_libraries(oxq_convert_cbl_container_test PRIVATE OXQF::convert)
target_compile_definitions(
  oxq_convert_cbl_container_test
  PRIVATE OXQF_GOLD_BASELINE_DIRECTORY="${PROJECT_SOURCE_DIR}/test/gold-baseline"
)
oxqf_set_project_warnings(oxq_convert_cbl_container_test)
oxqf_enable_sanitizers(oxq_convert_cbl_container_test)
add_test(NAME convert.cbl-container COMMAND oxq_convert_cbl_container_test)

add_executable(oxq_convert_cbl_record_test convert/cbl_record_test.cpp)
target_link_libraries(oxq_convert_cbl_record_test PRIVATE OXQF::convert)
target_include_directories(
  oxq_convert_cbl_record_test
  PRIVATE "${PROJECT_SOURCE_DIR}/src/convert/src"
)
target_compile_definitions(
  oxq_convert_cbl_record_test
  PRIVATE OXQF_GOLD_BASELINE_DIRECTORY="${PROJECT_SOURCE_DIR}/test/gold-baseline"
)
oxqf_set_project_warnings(oxq_convert_cbl_record_test)
oxqf_enable_sanitizers(oxq_convert_cbl_record_test)
add_test(NAME convert.cbl-record COMMAND oxq_convert_cbl_record_test)

add_executable(oxq_convert_cbl_tree_test convert/cbl_tree_test.cpp)
target_link_libraries(oxq_convert_cbl_tree_test PRIVATE OXQF::convert)
target_include_directories(
  oxq_convert_cbl_tree_test
  PRIVATE "${PROJECT_SOURCE_DIR}/src/convert/src"
)
target_compile_definitions(
  oxq_convert_cbl_tree_test
  PRIVATE OXQF_GOLD_BASELINE_DIRECTORY="${PROJECT_SOURCE_DIR}/test/gold-baseline"
)
oxqf_set_project_warnings(oxq_convert_cbl_tree_test)
oxqf_enable_sanitizers(oxq_convert_cbl_tree_test)
add_test(NAME convert.cbl-tree COMMAND oxq_convert_cbl_tree_test)

add_executable(oxq_convert_cbl_reader_test convert/cbl_reader_test.cpp)
target_link_libraries(oxq_convert_cbl_reader_test PRIVATE OXQF::convert)
target_compile_definitions(
  oxq_convert_cbl_reader_test
  PRIVATE OXQF_GOLD_BASELINE_DIRECTORY="${PROJECT_SOURCE_DIR}/test/gold-baseline"
)
oxqf_set_project_warnings(oxq_convert_cbl_reader_test)
oxqf_enable_sanitizers(oxq_convert_cbl_reader_test)
add_test(NAME convert.cbl-reader COMMAND oxq_convert_cbl_reader_test)

add_executable(
  oxq_convert_cbl_writer_preflight_test
  convert/cbl_writer_preflight_test.cpp
)
target_link_libraries(oxq_convert_cbl_writer_preflight_test PRIVATE OXQF::convert)
oxqf_set_project_warnings(oxq_convert_cbl_writer_preflight_test)
oxqf_enable_sanitizers(oxq_convert_cbl_writer_preflight_test)
add_test(
  NAME convert.cbl-writer-preflight
  COMMAND oxq_convert_cbl_writer_preflight_test
)

add_executable(oxq_convert_cbl_record_writer_test convert/cbl_record_writer_test.cpp)
target_link_libraries(oxq_convert_cbl_record_writer_test PRIVATE OXQF::convert)
target_include_directories(
  oxq_convert_cbl_record_writer_test
  PRIVATE "${PROJECT_SOURCE_DIR}/src/convert/src"
)
oxqf_set_project_warnings(oxq_convert_cbl_record_writer_test)
oxqf_enable_sanitizers(oxq_convert_cbl_record_writer_test)
add_test(NAME convert.cbl-record-writer COMMAND oxq_convert_cbl_record_writer_test)

add_executable(oxq_convert_cbl_tree_writer_test convert/cbl_tree_writer_test.cpp)
target_link_libraries(oxq_convert_cbl_tree_writer_test PRIVATE OXQF::convert)
target_include_directories(
  oxq_convert_cbl_tree_writer_test
  PRIVATE "${PROJECT_SOURCE_DIR}/src/convert/src"
)
oxqf_set_project_warnings(oxq_convert_cbl_tree_writer_test)
oxqf_enable_sanitizers(oxq_convert_cbl_tree_writer_test)
add_test(NAME convert.cbl-tree-writer COMMAND oxq_convert_cbl_tree_writer_test)

add_executable(oxq_convert_cbl_writer_test convert/cbl_writer_test.cpp)
target_link_libraries(oxq_convert_cbl_writer_test PRIVATE OXQF::convert)
target_include_directories(
  oxq_convert_cbl_writer_test
  PRIVATE "${PROJECT_SOURCE_DIR}/src/convert/src"
)
oxqf_set_project_warnings(oxq_convert_cbl_writer_test)
oxqf_enable_sanitizers(oxq_convert_cbl_writer_test)
add_test(NAME convert.cbl-writer COMMAND oxq_convert_cbl_writer_test)
set_tests_properties(
  convert.cbl-writer
  PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
)

add_executable(
  oxq_convert_cbl_writer_limits_test
  convert/cbl_writer_limits_test.cpp
)
target_link_libraries(oxq_convert_cbl_writer_limits_test PRIVATE OXQF::convert)
oxqf_set_project_warnings(oxq_convert_cbl_writer_limits_test)
oxqf_enable_sanitizers(oxq_convert_cbl_writer_limits_test)
add_test(
  NAME convert.cbl-writer-limits
  COMMAND oxq_convert_cbl_writer_limits_test
)
set_tests_properties(
  convert.cbl-writer-limits
  PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
)

add_executable(
  oxq_convert_cbl_writer_compatibility_vectors
  convert/cbl_writer_compatibility_vectors.cpp
)
target_link_libraries(
  oxq_convert_cbl_writer_compatibility_vectors
  PRIVATE OXQF::convert
)
oxqf_set_project_warnings(oxq_convert_cbl_writer_compatibility_vectors)
oxqf_enable_sanitizers(oxq_convert_cbl_writer_compatibility_vectors)

add_executable(
  oxq_convert_cbl_semantic_baseline_test
  convert/cbl_semantic_baseline_test.cpp
)
target_link_libraries(oxq_convert_cbl_semantic_baseline_test PRIVATE OXQF::convert)
target_compile_definitions(
  oxq_convert_cbl_semantic_baseline_test
  PRIVATE OXQF_GOLD_BASELINE_DIRECTORY="${PROJECT_SOURCE_DIR}/test/gold-baseline"
)
oxqf_set_project_warnings(oxq_convert_cbl_semantic_baseline_test)
oxqf_enable_sanitizers(oxq_convert_cbl_semantic_baseline_test)
add_test(
  NAME convert.cbl-semantic-baseline
  COMMAND oxq_convert_cbl_semantic_baseline_test
)

add_executable(oxq_convert_cbl_corpus_test convert/cbl_corpus_test.cpp)
target_link_libraries(oxq_convert_cbl_corpus_test PRIVATE OXQF::convert)
oxqf_set_project_warnings(oxq_convert_cbl_corpus_test)

if(OXQF_CBL_CORPUS_DIRECTORY)
  add_test(
    NAME convert.cbl-corpus
    COMMAND
      oxq_convert_cbl_corpus_test
      "${OXQF_CBL_CORPUS_DIRECTORY}"
      --release-baseline
  )
endif()

add_test(NAME cli.version COMMAND oxq --version)
set_tests_properties(cli.version PROPERTIES PASS_REGULAR_EXPRESSION "^oxq ${PROJECT_VERSION}")

add_executable(oxq_cli_test cli/cli_test.cpp)
target_link_libraries(oxq_cli_test PRIVATE oxq_cli_support)
target_include_directories(
  oxq_cli_test
  PRIVATE "${PROJECT_SOURCE_DIR}/src/cli"
)
target_compile_definitions(
  oxq_cli_test
  PRIVATE
    OXQF_VECTOR_DIRECTORY="${PROJECT_SOURCE_DIR}/test/vectors/oxq-v1"
    OXQF_GOLD_BASELINE_DIRECTORY="${PROJECT_SOURCE_DIR}/test/gold-baseline"
    OXQF_CLI_TEST_DIRECTORY="${PROJECT_BINARY_DIR}/cli-contract-test"
)
oxqf_set_project_warnings(oxq_cli_test)
oxqf_enable_sanitizers(oxq_cli_test)
add_test(NAME cli.contract-and-e2e COMMAND oxq_cli_test)

if(OXQF_ENABLE_SANITIZERS AND NOT MSVC)
  set_tests_properties(
    core.header-smoke
    core.game-model
    core.state-validation
    core.unicode-nfc
    core.codec-container
    core.codec-string-pool
    core.codec-metadata
    core.codec-restricted-json
    core.codec-position
    core.codec-annotation
    core.codec-move-tree
    core.reader
    core.validator
    core.writer
    core.malformed-regression
    core.deep-malformed
    convert.header-smoke
    convert.cbl-container
    convert.cbl-record
    convert.cbl-tree
    convert.cbl-reader
    convert.cbl-writer-preflight
    convert.cbl-record-writer
    convert.cbl-tree-writer
    convert.cbl-semantic-baseline
    cli.version
    cli.contract-and-e2e
    PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
  )
endif()

find_program(OXQF_NODE_EXECUTABLE NAMES node REQUIRED)
add_test(
  NAME cli.json-and-thin-wrapper
  COMMAND
    "${OXQF_NODE_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/test/cli/process_test.mjs"
    "${PROJECT_BINARY_DIR}/cli-contract-test/json"
    "${PROJECT_SOURCE_DIR}"
)
set_tests_properties(
  cli.json-and-thin-wrapper
  PROPERTIES DEPENDS cli.contract-and-e2e
)

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
