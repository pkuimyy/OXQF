function(oxqf_set_project_warnings target_name)
  if(MSVC)
    target_compile_options(${target_name} PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(
      ${target_name}
      PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
    )
  endif()
endfunction()

function(oxqf_enable_sanitizers target_name)
  if(NOT OXQF_ENABLE_SANITIZERS)
    return()
  endif()

  if(MSVC)
    message(WARNING "OXQF_ENABLE_SANITIZERS is not configured for MSVC")
    return()
  endif()

  target_compile_options(
    ${target_name}
    PRIVATE
      -fsanitize=address,undefined
      -fno-omit-frame-pointer
  )

  get_target_property(target_type ${target_name} TYPE)
  if(target_type STREQUAL "STATIC_LIBRARY")
    target_link_options(${target_name} INTERFACE -fsanitize=address,undefined)
  else()
    target_link_options(${target_name} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()
