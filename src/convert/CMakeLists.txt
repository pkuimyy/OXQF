add_library(oxq_convert STATIC)
add_library(OXQF::convert ALIAS oxq_convert)

target_sources(
  oxq_convert
  PRIVATE
    src/cbl/container.cpp
    src/cbl/container_writer.cpp
    src/cbl/hash.cpp
    src/cbl/record.cpp
    src/cbl/record_writer.cpp
    src/cbl/tree.cpp
    src/cbl/tree_writer.cpp
    src/cbl_reader.cpp
    src/cbl_writer.cpp
    src/product.cpp
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
      include/oxq/convert/cbl_reader.hpp
      include/oxq/convert/cbl_writer.hpp
      include/oxq/convert/conversion_report.hpp
      include/oxq/convert/product.hpp
)

target_compile_features(oxq_convert PUBLIC cxx_std_20)
target_include_directories(oxq_convert PRIVATE src)
target_compile_definitions(oxq_convert PRIVATE OXQF_PROJECT_VERSION="${PROJECT_VERSION}")
target_link_libraries(oxq_convert PUBLIC OXQF::core)

set_target_properties(
  oxq_convert
  PROPERTIES
    EXPORT_NAME convert
    OUTPUT_NAME oxq-convert
    POSITION_INDEPENDENT_CODE ON
)

oxqf_set_project_warnings(oxq_convert)
oxqf_enable_sanitizers(oxq_convert)

install(
  TARGETS oxq_convert
  EXPORT OXQFTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  FILE_SET HEADERS DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
