# TinyJSON — single-header JSON library
# Include via: find_package(tinyjson CONFIG) or add_subdirectory

add_library(tinyjson INTERFACE)
target_include_directories(tinyjson INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(tinyjson INTERFACE cxx_std_17)
