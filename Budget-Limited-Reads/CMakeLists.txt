cmake_minimum_required(VERSION 3.16)
project(budget_limited_reads LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(mirror_engine STATIC
  src/engine/fake_backend.cpp
  src/mirror/mirror_reader.cpp
  src/mirror/verify.cpp
  src/policy/selector.cpp
  src/policy/hedge_fixed.cpp
  src/policy/hedge_budget.cpp
  src/accounting/counters.cpp
  src/accounting/logger.cpp
  src/workload/trace.cpp
)
target_include_directories(mirror_engine PUBLIC src)
if(MSVC)
  target_compile_options(mirror_engine PRIVATE /W4 /permissive-)
else()
  target_compile_options(mirror_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
  target_sources(mirror_engine PRIVATE src/engine/windows_backend.cpp)
  target_compile_definitions(mirror_engine PUBLIC BLR_HAVE_WINDOWS=1 NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(mirror_engine PUBLIC kernel32)
endif()

# Native Windows overlapped I/O and Linux io_uring share the request engine.
option(BLR_ENABLE_URING "Build Linux io_uring backend" ON)
option(BLR_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(BLR_SANITIZERS AND MSVC)
  message(FATAL_ERROR "BLR_SANITIZERS enables ASan+UBSan with GCC/Clang. Use OFF for MSVC.")
endif()
if(BLR_SANITIZERS)
  target_compile_options(mirror_engine PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(mirror_engine PUBLIC -fsanitize=address,undefined)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND BLR_ENABLE_URING)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(URING REQUIRED liburing)
  target_sources(mirror_engine PRIVATE src/engine/uring_backend.cpp)
  target_compile_definitions(mirror_engine PUBLIC BLR_HAVE_URING=1)
  target_include_directories(mirror_engine PUBLIC ${URING_INCLUDE_DIRS})
  target_link_libraries(mirror_engine PUBLIC ${URING_LIBRARIES})
endif()

add_executable(blr src/main.cpp)
target_link_libraries(blr PRIVATE mirror_engine)

add_executable(gen_trace src/workload/gen_trace.cpp)
target_link_libraries(gen_trace PRIVATE mirror_engine)

enable_testing()
add_executable(blr_tests tests/test_main.cpp)
target_link_libraries(blr_tests PRIVATE mirror_engine)
add_test(NAME blr_tests COMMAND blr_tests)

add_executable(gen_data src/workload/gen_data.cpp)
target_link_libraries(gen_data PRIVATE mirror_engine)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME cli_smoke COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_smoke.py $<TARGET_FILE:blr> $<TARGET_FILE:gen_trace> $<TARGET_FILE:gen_data>)
endif()

if(WIN32)
  add_executable(blr_windows_tests tests/windows_backend_tests.cpp)
  target_link_libraries(blr_windows_tests PRIVATE mirror_engine)
  add_test(NAME windows_native_reads COMMAND blr_windows_tests)
  set_tests_properties(windows_native_reads PROPERTIES TIMEOUT 120)
endif()

if(WIN32)
  add_executable(blr_hdd src/workload/windows_hdd_runner.cpp)
  target_compile_definitions(blr_hdd PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(blr_hdd PRIVATE kernel32)
  if(MSVC)
    target_compile_options(blr_hdd PRIVATE /W4 /permissive-)
  else()
    target_compile_options(blr_hdd PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME windows_hdd_help COMMAND blr_hdd --help)
endif()
