add_library(oxq_cli_support STATIC cli.cpp json.cpp names.cpp)
target_compile_features(oxq_cli_support PRIVATE cxx_std_20)
target_link_libraries(oxq_cli_support PUBLIC OXQF::core OXQF::convert)
oxqf_set_project_warnings(oxq_cli_support)
oxqf_enable_sanitizers(oxq_cli_support)

add_executable(oxq main.cpp)

target_compile_features(oxq PRIVATE cxx_std_20)
target_link_libraries(oxq PRIVATE oxq_cli_support)

oxqf_set_project_warnings(oxq)
oxqf_enable_sanitizers(oxq)

install(TARGETS oxq RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
