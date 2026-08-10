add_library(oxq_core STATIC)
add_library(OXQF::core ALIAS oxq_core)

target_sources(
  oxq_core
  PRIVATE
    src/product.cpp
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES include/oxq/core/product.hpp
)

target_compile_features(oxq_core PUBLIC cxx_std_20)
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

install(
  TARGETS oxq_core
  EXPORT OXQFTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  FILE_SET HEADERS DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
