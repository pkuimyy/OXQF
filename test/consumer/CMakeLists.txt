cmake_minimum_required(VERSION 3.23)

project(OXQFConsumerSmoke LANGUAGES CXX)

find_package(OXQF CONFIG REQUIRED)

add_executable(oxqf_consumer_smoke main.cpp)
target_compile_features(oxqf_consumer_smoke PRIVATE cxx_std_20)
target_link_libraries(oxqf_consumer_smoke PRIVATE OXQF::core OXQF::convert)
