foreach(required_variable IN ITEMS OXQF_BUILD_DIR OXQF_SOURCE_DIR OXQF_GENERATOR)
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
