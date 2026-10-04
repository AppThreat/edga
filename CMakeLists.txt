# edga: the EDG C/C++ front end with a back end that writes each translation unit's program as
# JSON. The front end's sources are built from the pinned submodule, unmodified.

cmake_minimum_required(VERSION 3.19)

project(edga VERSION 0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS NO)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(EDG_ROOT ${CMAKE_SOURCE_DIR}/third_party/edgcpp)
if(NOT EXISTS ${EDG_ROOT}/src/cfe.c)
  message(FATAL_ERROR
          "The EDG sources are missing: run `git submodule update --init`.")
endif()

# The front end's configuration for this host (config/macro-conf/<name>).
if(NOT DEFINED EDGA_MACRO_CONF)
  if(APPLE)
    set(EDGA_MACRO_CONF macos-arm64)
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(EDGA_MACRO_CONF linux-aarch64)
  else()
    set(EDGA_MACRO_CONF linux-x86_64)
  endif()
endif()
set(EDGA_MACRO_CONF ${EDGA_MACRO_CONF} CACHE STRING
    "Front end configuration under config/macro-conf")
message(STATUS "edga front end configuration: ${EDGA_MACRO_CONF}")

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# edg-cmakedef resolves `import <...>` against one root, so the root is assembled in the build
# tree: EDG's shared configuration next to ours.
set(MACRO_CONF_ROOT ${CMAKE_BINARY_DIR}/macro-conf)
file(MAKE_DIRECTORY ${MACRO_CONF_ROOT})
foreach(entry support default)
  if(NOT EXISTS ${MACRO_CONF_ROOT}/${entry})
    file(CREATE_LINK ${EDG_ROOT}/cmake/macro-conf/${entry}
         ${MACRO_CONF_ROOT}/${entry} SYMBOLIC COPY_ON_ERROR)
  endif()
endforeach()
if(NOT EXISTS ${MACRO_CONF_ROOT}/edga)
  file(CREATE_LINK ${CMAKE_SOURCE_DIR}/config/macro-conf ${MACRO_CONF_ROOT}/edga
       SYMBOLIC COPY_ON_ERROR)
endif()

set(GENERATED_DIR ${CMAKE_BINARY_DIR}/includes)
file(MAKE_DIRECTORY ${GENERATED_DIR})

function(run_cmakedef mode out_var)
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH=${EDG_ROOT}/dev_tools/pylibs
            ${Python3_EXECUTABLE} ${EDG_ROOT}/dev_tools/bin/edg-cmakedef
            ${MACRO_CONF_ROOT} ${MACRO_CONF_ROOT}/edga/${EDGA_MACRO_CONF}/base.cmakedef
            ${mode}
    OUTPUT_VARIABLE output
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE status)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "edg-cmakedef ${mode} failed for ${EDGA_MACRO_CONF}")
  endif()
  set(${out_var} "${output}" PARENT_SCOPE)
endfunction()

# Reconfigure when any configuration file changes.
run_cmakedef(print-paths watched)
string(REPLACE "\n" ";" watched "${watched}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${watched})

# The configuration as a header, as EDG's own build writes it. EDG's configuration refers to its
# own source directory as @CMAKE_SOURCE_DIR@.
function(write_cmake_defines)
  run_cmakedef(print-defines defines)
  set(CMAKE_SOURCE_DIR ${EDG_ROOT})
  string(CONFIGURE "${defines}" defines)
  file(WRITE ${GENERATED_DIR}/cmake_defines.h.new "${defines}\n")
  file(COPY_FILE ${GENERATED_DIR}/cmake_defines.h.new ${GENERATED_DIR}/cmake_defines.h
       ONLY_IF_DIFFERENT)
  file(SHA256 ${GENERATED_DIR}/cmake_defines.h fingerprint)
  string(SUBSTRING ${fingerprint} 0 16 fingerprint)
  set(EDGA_CONFIG_FINGERPRINT ${fingerprint} PARENT_SCOPE)
endfunction()
write_cmake_defines()

execute_process(COMMAND git -C ${EDG_ROOT} rev-parse HEAD
                OUTPUT_VARIABLE EDGA_EDG_COMMIT
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE git_status
                ERROR_QUIET)
if(NOT git_status EQUAL 0)
  set(EDGA_EDG_COMMIT unknown)
endif()
configure_file(${CMAKE_SOURCE_DIR}/src/edga_version.h.in
               ${GENERATED_DIR}/edga_version.h @ONLY)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(EDG_COMPILE_OPTIONS -x c++ -fno-exceptions -fno-unwind-tables -fno-rtti -w)
  set(EDGA_COMPILE_OPTIONS -fno-exceptions -fno-unwind-tables -fno-rtti -Wall
                           -Wno-unused-parameter)
  if(UNIX AND NOT APPLE)
    # A self-contained, stripped binary for distribution.
    add_link_options("$<$<CONFIG:Release>:-static>" "$<$<CONFIG:Release>:-s>")
    # On Linux the front end tests for NaN with glibc's __isnan, which musl does not have.
    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles("#include <math.h>\nint main() { return __isnan(0.0); }"
                              EDGA_HAVE___ISNAN)
    if(NOT EDGA_HAVE___ISNAN)
      list(APPEND EDG_COMPILE_OPTIONS -D__isnan=__builtin_isnan)
    endif()
  endif()
else()
  set(EDG_COMPILE_OPTIONS /bigobj /w)
  set(EDGA_COMPILE_OPTIONS /bigobj /W3)
endif()

# EDG's error message tables, generated from its message files.
add_executable(mk_errinfo ${EDG_ROOT}/util/mk_errinfo.c)
set_source_files_properties(${EDG_ROOT}/util/mk_errinfo.c PROPERTIES LANGUAGE CXX)
target_compile_options(mk_errinfo PRIVATE ${EDG_COMPILE_OPTIONS})
target_include_directories(mk_errinfo PRIVATE ${EDG_ROOT}/src)
set(ERROR_TEXT_FILES ${EDG_ROOT}/src/error_msg.txt ${EDG_ROOT}/src/error_tag.txt)
set(ERROR_HEADERS ${GENERATED_DIR}/err_codes.h ${GENERATED_DIR}/err_data.h)
add_custom_command(
  OUTPUT ${ERROR_HEADERS}
  COMMAND mk_errinfo ${ERROR_TEXT_FILES} ${ERROR_HEADERS}
  DEPENDS mk_errinfo ${ERROR_TEXT_FILES}
  COMMENT "Generating EDG's error message tables")
add_custom_target(error-info DEPENDS ${ERROR_HEADERS})

# The front end's sources: the list EDG's own build compiles, read from it so a submodule update
# brings new files along.
file(READ ${EDG_ROOT}/src/CMakeLists.txt edg_src_cmake)
string(REGEX MATCH "set\\(CORE_FRONT_END_SOURCE_FILES([^)]*)\\)" core_match "${edg_src_cmake}")
if(NOT core_match)
  message(FATAL_ERROR "CORE_FRONT_END_SOURCE_FILES not found in EDG's src/CMakeLists.txt")
endif()
string(REGEX REPLACE "[ \t\r\n]+" ";" core_files "${CMAKE_MATCH_1}")
list(FILTER core_files EXCLUDE REGEX "^$")
string(REGEX MATCHALL "add_src_file_if_present\\(([a-z_0-9]+\\.c)\\)" optional_matches
       "${edg_src_cmake}")
foreach(m ${optional_matches})
  string(REGEX REPLACE "add_src_file_if_present\\(([a-z_0-9]+\\.c)\\)" "\\1" f "${m}")
  if(EXISTS ${EDG_ROOT}/src/${f})
    list(APPEND core_files ${f})
  endif()
endforeach()
list(TRANSFORM core_files PREPEND ${EDG_ROOT}/src/)
set_source_files_properties(${core_files} PROPERTIES LANGUAGE CXX)

add_library(edg_front_end STATIC ${core_files})
add_dependencies(edg_front_end error-info)
target_compile_options(edg_front_end PRIVATE ${EDG_COMPILE_OPTIONS})
target_compile_definitions(edg_front_end PUBLIC USE_CMAKE_DEFINES)
target_include_directories(edg_front_end PUBLIC ${GENERATED_DIR} ${EDG_ROOT}/src)

# Names for the IL's kind enums, generated from EDG's il_def.h so they follow its configuration.
add_custom_command(
  OUTPUT ${GENERATED_DIR}/il_names.inc
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/gen_il_names.py
          ${EDG_ROOT}/src/il_def.h > ${GENERATED_DIR}/il_names.inc
  DEPENDS ${CMAKE_SOURCE_DIR}/tools/gen_il_names.py ${EDG_ROOT}/src/il_def.h
  COMMENT "Generating IL kind names")
add_custom_target(il-names DEPENDS ${GENERATED_DIR}/il_names.inc)

add_executable(edga src/edga_main.cpp src/edga_back_end.cpp src/sarif_reader.cpp)
add_dependencies(edga il-names)
target_compile_options(edga PRIVATE ${EDGA_COMPILE_OPTIONS})
target_link_libraries(edga PRIVATE edg_front_end)
if(UNIX AND NOT APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
  target_link_libraries(edga PRIVATE quadmath)
endif()

install(TARGETS edga RUNTIME DESTINATION bin)
