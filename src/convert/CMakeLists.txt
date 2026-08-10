add_library(oxq_convert STATIC)
add_library(OXQF::convert ALIAS oxq_convert)

target_sources(
  oxq_convert
  PRIVATE
    src/product.cpp
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES include/oxq/convert/product.hpp
)

target_compile_features(oxq_convert PUBLIC cxx_std_20)
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
