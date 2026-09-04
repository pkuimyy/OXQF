cmake_minimum_required(VERSION 3.23)

project(
  OXQF
  VERSION 1.0.0
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
option(OXQF_BUILD_FUZZERS "Build Clang libFuzzer targets" OFF)
set(
  OXQF_CBL_CORPUS_DIRECTORY
  ""
  CACHE PATH
  "Optional directory containing the full release CBL corpus"
)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(MSVC AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

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
  COMPONENT Development
)

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/OXQFConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/OXQFConfigVersion.cmake"
  DESTINATION "${OXQF_INSTALL_CMAKEDIR}"
  COMPONENT Development
)

install(
  FILES
    README.md
    CHANGELOG.md
    LICENSE
    doc/cli.md
    doc/mvp-acceptance.md
    doc/release.md
  DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/OXQF"
  COMPONENT Documentation
)
install(
  DIRECTORY spec/
  DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/OXQF/spec"
  COMPONENT Documentation
  FILES_MATCHING PATTERN "*.md"
)
install(
  DIRECTORY test/vectors/oxq-v1/
  DESTINATION "${CMAKE_INSTALL_DATADIR}/OXQF/vectors/oxq-v1"
  COMPONENT Vectors
)
install(
  DIRECTORY test/gold-baseline/
  DESTINATION "${CMAKE_INSTALL_DATADIR}/OXQF/vectors/cbl-v3"
  COMPONENT Vectors
)
install(
  FILES test/writer-compatibility/README.md
  DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/OXQF/writer-compatibility"
  COMPONENT Documentation
)

set(CPACK_PACKAGE_NAME "OXQF")
set(CPACK_PACKAGE_VENDOR "Open Xiangqi Format contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "OXQ v1 reference libraries and command-line tools")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME
    "OXQF-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
if(WIN32)
  set(CPACK_GENERATOR ZIP)
else()
  set(CPACK_GENERATOR TGZ)
endif()
include(CPack)
