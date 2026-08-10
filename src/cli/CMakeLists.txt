add_executable(oxq main.cpp)

target_compile_features(oxq PRIVATE cxx_std_20)
target_link_libraries(oxq PRIVATE OXQF::core OXQF::convert)

oxqf_set_project_warnings(oxq)
oxqf_enable_sanitizers(oxq)

install(TARGETS oxq RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
