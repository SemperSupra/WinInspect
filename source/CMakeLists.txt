cmake_minimum_required(VERSION 3.22)
# Version must match core/include/wininspect/types.hpp (WININSPECT_VERSION)
project(WinInspect VERSION 0.4.0 LANGUAGES CXX)

enable_testing()

option(WININSPECT_BUILD_TESTS "Build tests" ON)
option(WININSPECT_USE_OPENSSL "Use OpenSSL for SSH key signing (static linking)" ON)

# Third-party dependencies
add_subdirectory(third_party/tinyjson)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows cross-compile compatibility
if(WIN32)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00 WINVER=0x0A00)
endif()
if(MSVC)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
  add_compile_options(/utf-8 /wd4244 /wd4996 /wd4102 /W3 /WX)
endif()
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Werror -Wno-unused-parameter -Wno-unused-variable -Wno-missing-field-initializers -Wno-cast-function-type -Wno-unknown-pragmas -Wno-unused-function -Wno-unused-but-set-variable)
  # ThreadSanitizer (TSAN) is available on GCC/Clang:
  # cmake -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" -DWININSPECT_BUILD_TESTS=ON
endif()

# Optional OpenSSL dependency (static linking = no DLLs needed at runtime)
if(WININSPECT_USE_OPENSSL)
  set(OPENSSL_USE_STATIC_LIBS TRUE)
  find_package(OpenSSL QUIET)
  if(OpenSSL_FOUND)
    add_compile_definitions(WININSPECT_HAVE_OPENSSL)
    message(STATUS "OpenSSL found: ${OPENSSL_VERSION} (static)")
  else()
    message(STATUS "OpenSSL NOT found — SSH key signing will be a stub.")
    message(STATUS "Install OpenSSL: winget install OpenSSL.OpenSSL")
  endif()
endif()

add_library(wininspect_core
  core/src/core.cpp
  core/src/fake_backend.cpp
  core/src/win32_backend.cpp
  core/src/crypto.cpp
  core/src/logger.cpp
  core/src/update.cpp
  core/src/network_config.cpp
  core/src/cidr.cpp
  core/src/mdns.cpp
  core/src/cert.cpp
  core/src/websocket.cpp
  core/src/compress.cpp
  core/src/tailscale.cpp
  core/src/tls.cpp
  core/src/credential_manager.cpp
  core/src/metrics.cpp
)
target_include_directories(wininspect_core PUBLIC
  core/include
)
target_include_directories(wininspect_core PRIVATE
  third_party
)

if (WIN32)
  target_link_libraries(wininspect_core PUBLIC ole32 oleaut32 uuid bcrypt winhttp dxgi d3d11 ws2_32 advapi32 crypt32)
  if(WININSPECT_USE_OPENSSL AND OpenSSL_FOUND)
    target_link_libraries(wininspect_core PUBLIC OpenSSL::Crypto OpenSSL::SSL)
  endif()
  if(NOT MSVC)
    target_link_options(wininspect_core PUBLIC -static-libgcc -static-libstdc++)
  endif()
endif()

if(WININSPECT_USE_OPENSSL)
  target_link_libraries(wininspect_core PUBLIC OpenSSL::Crypto OpenSSL::SSL)
  # Static linking only — no OpenSSL DLLs needed at runtime.
  # OPENSSL_USE_STATIC_LIBS is set above; FindOpenSSL handles the rest.
endif()

# Copy ASAN runtime DLLs to output directory (MSVC only)
# Note: ASAN DLLs may not be present on all runners (e.g. VS 2026 preview).
# The '|| ver>nul' ensures a zero exit code when no DLLs match.
if(MSVC AND CMAKE_CXX_FLAGS MATCHES "/fsanitize=address")
  add_custom_target(copy_asan_dlls ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/$<CONFIG>"
    COMMAND ${CMAKE_COMMAND} -E env "ASAN_DLL_DIR=${CMAKE_CXX_COMPILER}/../"
            cmd /v /c "for %f in (\"${CMAKE_CXX_COMPILER}/../*asan*.dll\") do copy /y \"%f\" \"${CMAKE_BINARY_DIR}/$<CONFIG>/\" 2>nul & if errorlevel 1 echo No ASAN DLLs found & ver>nul"
    COMMENT "Copying ASAN runtime DLLs to ${CMAKE_BINARY_DIR}/$<CONFIG>"
  )
endif()

if (WIN32)
  add_executable(wininspectd
    daemon/src/server.cpp
    daemon/src/pipe_win32.cpp
    daemon/src/indicator_manager.cpp
    daemon/src/tray.cpp
    daemon/src/tcp_server.cpp
    daemon/src/control_manager.cpp
    daemon/src/rendezvous_http.cpp
    daemon/src/http_server.cpp
    daemon/src/network_config.cpp
    resources/wininspect.rc
  )
  target_include_directories(wininspectd PRIVATE core/include third_party daemon/src daemon/include resources)
  target_link_libraries(wininspectd PRIVATE wininspect_core ws2_32)

  add_executable(wininspect
    clients/cli/src/cli.cpp
  )
  target_include_directories(wininspect PRIVATE core/include third_party)
  target_link_libraries(wininspect PRIVATE wininspect_core ws2_32)

  add_executable(wininspect-gui
    clients/gui/src/gui_main.cpp
    clients/gui/src/theme.cpp
    clients/gui/viewmodel/viewmodel.cpp
    resources/wininspect.rc
  )
  target_include_directories(wininspect-gui PRIVATE core/include third_party clients/gui/src clients/gui/viewmodel resources)
  target_link_libraries(wininspect-gui PRIVATE wininspect_core comctl32)
  set_target_properties(wininspect-gui PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if (WININSPECT_BUILD_TESTS)
  add_executable(test_core
    core/tests/main.cpp
    core/tests/test_core_idempotence.cpp
    core/tests/test_trace_replay.cpp
    core/tests/test_injection.cpp
    core/tests/test_crypto.cpp
    core/tests/test_uia.cpp
    core/tests/test_contract_methods.cpp
    core/tests/test_cli_contract.cpp
    core/tests/test_properties.cpp
    core/tests/test_fuzz.cpp
    core/tests/test_http.cpp
    core/tests/test_tcp_protocol.cpp
    core/tests/test_control.cpp
    core/tests/test_credential.cpp
    core/tests/test_config.cpp
    core/tests/test_tls.cpp
    core/tests/test_metrics.cpp
    core/tests/test_stress.cpp
    core/tests/test_mdns.cpp
    core/tests/test_rendezvous.cpp
    daemon/src/rendezvous_http.cpp
    daemon/src/control_manager.cpp
    daemon/src/network_config.cpp
  )
  target_include_directories(test_core PRIVATE core/include third_party third_party/rapidcheck daemon/include daemon/src)
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

  add_executable(test_discovery
    daemon/src/test_discovery.cpp
  )
  target_include_directories(test_discovery PRIVATE core/include)
  target_link_libraries(test_discovery PRIVATE ws2_32)
  if(NOT MSVC)
    target_link_options(test_discovery PUBLIC -static-libgcc -static-libstdc++)
  endif()
  add_test(NAME test_discovery COMMAND test_discovery WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  add_executable(benchmarks
    core/tests/benchmarks.cpp
  )
  target_include_directories(benchmarks PRIVATE core/include third_party)
  target_link_libraries(benchmarks PRIVATE wininspect_core)
  # Not added as a CTest test — run manually: ./benchmarks
endif()
