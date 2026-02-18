cmake_minimum_required(VERSION 3.10)
project(wine_uia_extension)

add_library(wine_uia_extension SHARED extension.cpp)

if(WIN32)
    # Define exports
    target_compile_definitions(wine_uia_extension PRIVATE "BUILDING_WINE_UIA_EXTENSION")
    # Link against standard libs
    target_link_libraries(wine_uia_extension ole32 uuid)
endif()
