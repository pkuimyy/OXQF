cmake_minimum_required(VERSION 3.23)

project(
  OXQF
  VERSION 0.1.3
  DESCRIPTION "Open Xiangqi Format reference implementation"
  LANGUAGES CXX
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not supported. Use cmake -S . -B build.")
endif()

include(CTest)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

option(OXQF_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(OXQFProjectOptions)

add_subdirectory(src/core)
add_subdirectory(src/convert)
add_subdirectory(src/cli)

if(BUILD_TESTING)
  add_subdirectory(test)
endif()

set(OXQF_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/OXQF")

configure_package_config_file(
  cmake/OXQFConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/OXQFConfig.cmake"
  INSTALL_DESTINATION "${OXQF_INSTALL_CMAKEDIR}"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/OXQFConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)

install(
  EXPORT OXQFTargets
  FILE OXQFTargets.cmake
  NAMESPACE OXQF::
  DESTINATION "${OXQF_INSTALL_CMAKEDIR}"
)

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/OXQFConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/OXQFConfigVersion.cmake"
  DESTINATION "${OXQF_INSTALL_CMAKEDIR}"
)

install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/OXQF")
