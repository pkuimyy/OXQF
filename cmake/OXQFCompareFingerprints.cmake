foreach(required_variable IN ITEMS OXQF_LINUX_FINGERPRINT OXQF_WINDOWS_FINGERPRINT)
  if(NOT DEFINED ${required_variable})
    message(FATAL_ERROR "Missing required variable: ${required_variable}")
  endif()
endforeach()

file(READ "${OXQF_LINUX_FINGERPRINT}" linux_fingerprint)
file(READ "${OXQF_WINDOWS_FINGERPRINT}" windows_fingerprint)
if(NOT linux_fingerprint STREQUAL windows_fingerprint)
  message(FATAL_ERROR
    "Linux and Windows OXQ Writer fingerprints differ:\n"
    "Linux:\n${linux_fingerprint}Windows:\n${windows_fingerprint}")
endif()
string(STRIP "${linux_fingerprint}" fingerprint)
message(STATUS "Linux/Windows Writer output is byte-identical: ${fingerprint}")
