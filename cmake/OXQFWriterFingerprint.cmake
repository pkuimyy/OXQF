foreach(required_variable IN ITEMS OXQF_WRITER_EXECUTABLE OXQF_REFERENCE OXQF_OUTPUT)
  if(NOT DEFINED ${required_variable})
    message(FATAL_ERROR "Missing required variable: ${required_variable}")
  endif()
endforeach()

set(generated "${OXQF_OUTPUT}.generated")
execute_process(
  COMMAND "${OXQF_WRITER_EXECUTABLE}" --dump
  OUTPUT_FILE "${generated}"
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  file(REMOVE "${generated}")
  message(FATAL_ERROR "Writer baseline generator failed with exit code ${result}")
endif()

# Windows text-mode stdout translates the diagnostic JSON's LF separators to
# CRLF. The OXQ bytes are hex-encoded inside that JSON, so normalize only the
# transport line endings before comparing and hashing the Writer evidence.
file(READ "${generated}" generated_text)
string(REPLACE "\r\n" "\n" generated_text "${generated_text}")
file(WRITE "${generated}" "${generated_text}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${generated}" "${OXQF_REFERENCE}"
  RESULT_VARIABLE comparison
)
if(NOT comparison EQUAL 0)
  file(REMOVE "${generated}")
  message(FATAL_ERROR "Writer output differs from the committed semantic baseline")
endif()

file(SHA256 "${generated}" sha256)
file(SIZE "${generated}" size)
file(WRITE "${OXQF_OUTPUT}" "sha256=${sha256}\nbytes=${size}\n")
file(REMOVE "${generated}")
message(STATUS "Writer fingerprint: ${sha256} (${size} bytes)")
