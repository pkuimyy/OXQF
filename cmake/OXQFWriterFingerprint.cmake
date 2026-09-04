foreach(required_variable IN ITEMS OXQF_WRITER_EXECUTABLE OXQF_OUTPUT)
  if(NOT DEFINED ${required_variable})
    message(FATAL_ERROR "Missing required variable: ${required_variable}")
  endif()
endforeach()

set(generated "${OXQF_OUTPUT}.generated")
execute_process(
  COMMAND "${OXQF_WRITER_EXECUTABLE}" --fingerprint
  OUTPUT_FILE "${generated}"
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  file(REMOVE "${generated}")
  message(FATAL_ERROR "Writer baseline generator failed with exit code ${result}")
endif()

file(READ "${generated}" generated_text)
string(REPLACE "\r\n" "\n" generated_text "${generated_text}")
if(NOT generated_text MATCHES
    "^sha256=[0-9a-f][0-9a-f]*\nframed_bytes=[0-9][0-9]*\n$")
  file(REMOVE "${generated}")
  message(FATAL_ERROR "Writer generator returned an invalid fingerprint")
endif()

file(WRITE "${OXQF_OUTPUT}" "${generated_text}")
file(REMOVE "${generated}")
string(STRIP "${generated_text}" fingerprint)
message(STATUS "Writer fingerprint: ${fingerprint}")
