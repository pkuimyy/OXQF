add_library(oxq_editor STATIC)
add_library(OXQF::editor ALIAS oxq_editor)

target_sources(
  oxq_editor
  PRIVATE
    src/editor_session.cpp
    src/product.cpp
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
      include/oxq/editor/command.hpp
      include/oxq/editor/editor_session.hpp
      include/oxq/editor/error.hpp
      include/oxq/editor/product.hpp
)

target_compile_features(oxq_editor PUBLIC cxx_std_20)
target_compile_definitions(oxq_editor PRIVATE OXQF_PROJECT_VERSION="${PROJECT_VERSION}")
target_link_libraries(oxq_editor PUBLIC OXQF::format)

set_target_properties(
  oxq_editor
  PROPERTIES
    EXPORT_NAME editor
    OUTPUT_NAME oxq-editor
    POSITION_INDEPENDENT_CODE ON
)

oxqf_set_project_warnings(oxq_editor)
oxqf_enable_sanitizers(oxq_editor)

install(
  TARGETS oxq_editor
  EXPORT OXQFTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT Development
  FILE_SET HEADERS DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" COMPONENT Development
)
