add_library(oxq_format STATIC)
add_library(OXQF::format ALIAS oxq_format)
# Source-tree compatibility for consumers migrating from the 1.x core name.
add_library(OXQF::core ALIAS oxq_format)

target_sources(
  oxq_format
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
      include/oxq/format/codec_error.hpp
      include/oxq/format/document.hpp
      include/oxq/format/product.hpp
      include/oxq/format/reader.hpp
      include/oxq/format/state_validation.hpp
      include/oxq/format/validation.hpp
      include/oxq/format/validator.hpp
      include/oxq/format/writer.hpp
)

target_compile_features(oxq_format PUBLIC cxx_std_20)
target_include_directories(oxq_format PRIVATE src)
target_compile_definitions(oxq_format PRIVATE OXQF_PROJECT_VERSION="${PROJECT_VERSION}")

set_target_properties(
  oxq_format
  PROPERTIES
    EXPORT_NAME format
    OUTPUT_NAME oxq-format
    POSITION_INDEPENDENT_CODE ON
)

oxqf_set_project_warnings(oxq_format)
oxqf_enable_sanitizers(oxq_format)

if(OXQF_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "OXQF_BUILD_FUZZERS requires Clang")
  endif()
  target_compile_options(oxq_format PRIVATE -fsanitize=fuzzer-no-link)
endif()

install(
  TARGETS oxq_format
  EXPORT OXQFTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT Development
  FILE_SET HEADERS DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" COMPONENT Development
)
