add_library(oxq_core STATIC)
add_library(OXQF::core ALIAS oxq_core)

target_sources(
  oxq_core
  PRIVATE
    src/codec/annotation.cpp
    src/codec/container.cpp
    src/codec/crc32c.cpp
    src/codec/metadata.cpp
    src/codec/move_tree.cpp
    src/codec/position.cpp
    src/codec/restricted_json.cpp
    src/codec/string_pool.cpp
    src/date_time.cpp
    src/extension_name.cpp
    src/game_model.cpp
    src/product.cpp
    src/reader.cpp
    src/state_validation.cpp
    src/utf8.cpp
    src/unicode_nfc.cpp
    src/validation.cpp
    src/validator.cpp
    src/writer.cpp
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
      include/oxq/core/game_model.hpp
      include/oxq/core/codec_error.hpp
      include/oxq/core/product.hpp
      include/oxq/core/reader.hpp
      include/oxq/core/state_validation.hpp
      include/oxq/core/validation.hpp
      include/oxq/core/validator.hpp
      include/oxq/core/writer.hpp
)

target_compile_features(oxq_core PUBLIC cxx_std_20)
target_include_directories(oxq_core PRIVATE src)
target_compile_definitions(oxq_core PRIVATE OXQF_PROJECT_VERSION="${PROJECT_VERSION}")

set_target_properties(
  oxq_core
  PROPERTIES
    EXPORT_NAME core
    OUTPUT_NAME oxq-core
    POSITION_INDEPENDENT_CODE ON
)

oxqf_set_project_warnings(oxq_core)
oxqf_enable_sanitizers(oxq_core)

if(OXQF_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "OXQF_BUILD_FUZZERS requires Clang")
  endif()
  target_compile_options(oxq_core PRIVATE -fsanitize=fuzzer-no-link)
endif()

install(
  TARGETS oxq_core
  EXPORT OXQFTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT Development
  FILE_SET HEADERS DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" COMPONENT Development
)
