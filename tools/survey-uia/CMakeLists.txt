cmake_minimum_required(VERSION 3.10)
project(survey_uia)

add_executable(survey_uia survey_uia.cpp)
if(WIN32)
    target_link_libraries(survey_uia ole32 oleaut32 uuid)
endif()
