foreach(required_variable IN ITEMS
    OXQF_BUILD_DIR
    OXQF_SOURCE_DIR
    OXQF_GENERATOR
    OXQF_INSTALL_INCLUDEDIR
    OXQF_INSTALL_LIBDIR
    OXQF_INSTALL_DATADIR)
  if(NOT DEFINED ${required_variable})
    message(FATAL_ERROR "Missing required variable: ${required_variable}")
  endif()
endforeach()

set(stage_dir "${OXQF_BUILD_DIR}/consumer-stage")
set(consumer_build_dir "${OXQF_BUILD_DIR}/consumer-build")

file(REMOVE_RECURSE "${stage_dir}" "${consumer_build_dir}")

set(config_arguments)
if(DEFINED OXQF_CONFIG AND NOT OXQF_CONFIG STREQUAL "")
  list(APPEND config_arguments --config "${OXQF_CONFIG}")
endif()

execute_process(
  COMMAND
    "${CMAKE_COMMAND}" --install "${OXQF_BUILD_DIR}"
    --prefix "${stage_dir}"
    ${config_arguments}
  COMMAND_ERROR_IS_FATAL ANY
)

include("${OXQF_SOURCE_DIR}/../release/verify_install.cmake")

if(WIN32)
  set(path_separator ";")
else()
  set(path_separator ":")
endif()
set(ENV{PATH} "${stage_dir}/bin${path_separator}$ENV{PATH}")
execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env "ASAN_OPTIONS=detect_leaks=0"
    oxq --version
  OUTPUT_VARIABLE oxq_version
  OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY
)
if(NOT oxq_version MATCHES "^oxq [0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "Installed oxq returned unexpected version: ${oxq_version}")
endif()

execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    -S "${OXQF_SOURCE_DIR}"
    -B "${consumer_build_dir}"
    -G "${OXQF_GENERATOR}"
    "-DCMAKE_PREFIX_PATH=${stage_dir}"
  COMMAND_ERROR_IS_FATAL ANY
)

execute_process(
  COMMAND
    "${CMAKE_COMMAND}" --build "${consumer_build_dir}"
    ${config_arguments}
  COMMAND_ERROR_IS_FATAL ANY
)
