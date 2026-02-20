cmake_minimum_required(VERSION 3.22)
project(WinInspect LANGUAGES CXX)

enable_testing()

option(WININSPECT_BUILD_TESTS "Build tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(wininspect_core
  core/src/core.cpp
  core/src/fake_backend.cpp
  core/src/win32_backend.cpp
  core/src/crypto.cpp
)
target_include_directories(wininspect_core PUBLIC
  core/include
)
target_include_directories(wininspect_core PRIVATE
  third_party
)

if (WIN32)
  target_link_libraries(wininspect_core PUBLIC ole32 oleaut32 uuid bcrypt)
  target_link_options(wininspect_core PUBLIC -static-libgcc -static-libstdc++)
endif()

if (WIN32)
  add_executable(wininspectd
    daemon/src/server.cpp
    daemon/src/pipe_win32.cpp
    daemon/src/tray.cpp
    daemon/src/tcp_server.cpp
  )
  target_include_directories(wininspectd PRIVATE core/include third_party daemon/src)
  target_link_libraries(wininspectd PRIVATE wininspect_core ws2_32)

  add_executable(wininspect
    clients/cli/src/cli.cpp
  )
  target_include_directories(wininspect PRIVATE core/include third_party)
  target_link_libraries(wininspect PRIVATE wininspect_core ws2_32)

  add_executable(wininspect-gui
    clients/gui/src/gui_main.cpp
    clients/gui/viewmodel/viewmodel.cpp
  )
  target_include_directories(wininspect-gui PRIVATE core/include third_party clients/gui/viewmodel)
  target_link_libraries(wininspect-gui PRIVATE wininspect_core comctl32)
endif()

if (WININSPECT_BUILD_TESTS)
  add_executable(test_core
    core/tests/main.cpp
    core/tests/test_core_idempotence.cpp
    core/tests/test_trace_replay.cpp
    core/tests/test_injection.cpp
    core/tests/test_crypto.cpp
    core/tests/test_uia.cpp
  )
  target_include_directories(test_core PRIVATE core/include third_party)
  target_link_libraries(test_core PRIVATE wininspect_core)
  add_test(NAME test_core COMMAND test_core WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  add_executable(test_gui_viewmodel
    clients/gui/tests/main.cpp
    clients/gui/tests/test_viewmodel.cpp
    clients/gui/viewmodel/viewmodel.cpp
  )
  target_include_directories(test_gui_viewmodel PRIVATE core/include third_party clients/gui/viewmodel)
  target_link_libraries(test_gui_viewmodel PRIVATE wininspect_core)
  add_test(NAME test_gui_viewmodel COMMAND test_gui_viewmodel WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
