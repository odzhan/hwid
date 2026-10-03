cmake_minimum_required(VERSION 3.24)
project(hwid VERSION 1.0.0 LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "hwid requires Windows and MSVC (Visual Studio 2022 or newer).")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Configure an x64 build.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_library(hwid_core
  src/common/Strings.cpp src/common/Error.cpp
  src/app/Cli.cpp src/output/Writers.cpp
  src/windows/WmiClient.cpp src/windows/DeviceEnumerator.cpp
  src/collectors/WmiCollectors.cpp src/collectors/StorageCollector.cpp
  src/collectors/NetworkCollector.cpp src/collectors/UsbCollector.cpp
  src/collectors/GpuCollector.cpp src/collectors/TpmCollector.cpp
  src/collectors/CpuCollector.cpp src/collectors/Collectors.cpp)
target_include_directories(hwid_core PUBLIC src)
target_compile_definitions(hwid_core PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_compile_options(hwid_core PUBLIC /W4 /permissive- /utf-8 /EHsc)
target_link_libraries(hwid_core PUBLIC wbemuuid ole32 oleaut32 iphlpapi setupapi cfgmgr32 dxgi tbs ncrypt bcrypt)
add_executable(hwid src/main.cpp)
target_link_libraries(hwid PRIVATE hwid_core)
include(CTest)
if(BUILD_TESTING)
  add_executable(hwid_tests tests/Tests.cpp)
  target_link_libraries(hwid_tests PRIVATE hwid_core)
  add_test(NAME unit_and_fixtures COMMAND hwid_tests)
  add_test(NAME cli_help COMMAND hwid --help)
  add_test(NAME cli_reject_unknown COMMAND hwid --component invalid)
  set_tests_properties(cli_reject_unknown PROPERTIES WILL_FAIL TRUE)
  add_test(NAME inventory_json COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/Integration.ps1 -Executable $<TARGET_FILE:hwid>)
  set_tests_properties(inventory_json PROPERTIES TIMEOUT 180 LABELS integration)
endif()
install(TARGETS hwid RUNTIME DESTINATION bin)
