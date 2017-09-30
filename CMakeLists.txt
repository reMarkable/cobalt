# Copyright 2016 The Fuchsia Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required (VERSION 3.0.0)

# The only way we found to override the compiler is setting these variables
# prior to the project definition.  Otherwise enable_language() sets these
# variables and gcc will be picked up instead.  Also, new versions of cmake will
# complain if one compiler is first set (say to gcc via enable_language), and
# then we later switch it to (say) clang.
set(CMAKE_C_COMPILER "clang")
set(CMAKE_CXX_COMPILER "clang++")

project(cobalt)

include(ExternalProject)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -Wsign-compare -Wignored-qualifiers -std=c++11 -stdlib=libstdc++ -pthread -fcolor-diagnostics")
enable_language(C)
enable_language(CXX)

# Tell the file logging.h that we want to use Google's GLog library for
# logging.
add_definitions(-DHAVE_GLOG=1)

set(DIR_GTESTS "${CMAKE_BINARY_DIR}/gtests")
set(DIR_SYSROOT "${CMAKE_SOURCE_DIR}/sysroot")
set(DIR_END_TO_END_TESTS "${CMAKE_BINARY_DIR}/e2e_tests")
set(DIR_GTESTS_BT_EMULATOR "${CMAKE_BINARY_DIR}/gtests_btemulator")
set(DIR_GTESTS_CLOUD_BT "${CMAKE_BINARY_DIR}/gtests_cloud_bt")
set(DIR_PERF_TESTS "${CMAKE_BINARY_DIR}/perf_tests")

# Go related defines
set(GO_PATH env GOPATH="${DIR_SYSROOT}/go:${CMAKE_SOURCE_DIR}/third_party/go:${CMAKE_BINARY_DIR}/go-proto-gen")
set(GO_PATH env "${GO_PATH}:${CMAKE_SOURCE_DIR}/shuffler")
set(GO_PATH env "${GO_PATH}:${CMAKE_SOURCE_DIR}/tools/go")
set(GO_BIN ${GO_PATH} go)
set(GO_TESTS "${CMAKE_BINARY_DIR}/go_tests")
set(GO_PROTO_GEN_SRC_DIR "${CMAKE_BINARY_DIR}/go-proto-gen/src")

file(MAKE_DIRECTORY ${GO_PROTO_GEN_SRC_DIR})
file(MAKE_DIRECTORY ${GO_TESTS})

link_directories(${DIR_SYSROOT}/lib)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_BINARY_DIR})
include_directories(BEFORE SYSTEM ${DIR_SYSROOT}/include)

# Build googletest as an external project.
set(GTEST_INSTALL_DIR ${CMAKE_BINARY_DIR}/third_party/googletest/googletest)
set(GTEST_INCLUDE_DIR ${GTEST_INSTALL_DIR}/include)
set(GTEST_LIB_DIR ${GTEST_INSTALL_DIR}/lib)
ExternalProject_Add(gtest_external_project
                    SOURCE_DIR  ${CMAKE_SOURCE_DIR}/third_party/googletest/googletest
                    PREFIX      ${GTEST_INSTALL_DIR}
                    INSTALL_DIR ${GTEST_INSTALL_DIR}
                    CMAKE_ARGS  -DCMAKE_INSTALL_PREFIX:PATH=${GTEST_INSTALL_DIR}
                                -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                                -Wno-sign-compare -Wno-ignored-qualifiers
                   )
include_directories(BEFORE SYSTEM ${GTEST_INCLUDE_DIR})
link_directories(${GTEST_LIB_DIR})

# Build gflags as an external project.
set(GFLAGS_INSTALL_DIR ${CMAKE_BINARY_DIR}/third_party/gflags)
set(GFLAGS_INCLUDE_DIR ${GFLAGS_INSTALL_DIR}/include)
set(GFLAGS_LIB_DIR ${GFLAGS_INSTALL_DIR}/lib)
ExternalProject_Add(gflags_external_project
                    SOURCE_DIR  ${CMAKE_SOURCE_DIR}/third_party/gflags
                    PREFIX      ${GFLAGS_INSTALL_DIR}
                    INSTALL_DIR ${GFLAGS_INSTALL_DIR}
                    CMAKE_ARGS  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                                -DCMAKE_INSTALL_PREFIX:PATH=${GFLAGS_INSTALL_DIR}
                                -Wno-sign-compare -Wno-ignored-qualifiers
                                -Wno-deprecated-register -Wno-unused-function
                                -Wno-unused-local-typedef
                   )
include_directories(BEFORE SYSTEM ${GFLAGS_INCLUDE_DIR})
link_directories(${GFLAGS_LIB_DIR})

# Build glog as an external project.
set(GLOG_INSTALL_DIR ${CMAKE_BINARY_DIR}/third_party/glog)
set(GLOG_INCLUDE_DIR ${GLOG_INSTALL_DIR}/include)
set(GLOG_LIB_DIR ${GLOG_INSTALL_DIR}/lib)
ExternalProject_Add(glog_external_project
                    SOURCE_DIR  ${CMAKE_SOURCE_DIR}/third_party/glog
                    PREFIX      ${GLOG_INSTALL_DIR}
                    INSTALL_DIR ${GLOG_INSTALL_DIR}
                    CMAKE_ARGS  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                                -Dgflags_DIR=${GFLAGS_INSTALL_DIR}
                                -DCMAKE_INSTALL_PREFIX:PATH=${GLOG_INSTALL_DIR}
                                -Wno-sign-compare -Wno-ignored-qualifiers
                                -Wno-deprecated-register -Wno-unused-function
                                -Wno-unused-local-typedef
                    DEPENDS     gflags_external_project
                   )
include_directories(BEFORE SYSTEM ${GLOG_INCLUDE_DIR})
link_directories(${GLOG_LIB_DIR})

# Build boringssl as an external project.
set(BORINGSSL_INSTALL_DIR ${CMAKE_BINARY_DIR}/third_party/boringssl)
set(BORINGSSL_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/third_party/boringssl/include)
set(CRYPTO_LIB_DIR ${BORINGSSL_INSTALL_DIR}/src/boringssl_external_project-build/crypto)
set(SSL_LIB_DIR ${BORINGSSL_INSTALL_DIR}/src/boringssl_external_project-build/ssl)
ExternalProject_Add(boringssl_external_project
                    SOURCE_DIR  ${CMAKE_SOURCE_DIR}/third_party/boringssl
                    PREFIX      ${BORINGSSL_INSTALL_DIR}
                    INSTALL_DIR ${BORINGSSL_INSTALL_DIR}
                    CMAKE_ARGS  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                                -DCMAKE_INSTALL_PREFIX:PATH=${BORINGSSL_INSTALL_DIR}
                                -Wno-sign-compare -Wno-ignored-qualifiers
                                -Wno-deprecated-register -Wno-unused-function
                                -Wno-unused-local-typedef
                    DEPENDS     gflags_external_project
                    DEPENDS     glog_external_project
                    DEPENDS     gtest_external_project
                    INSTALL_COMMAND ""
                   )
include_directories(BEFORE SYSTEM ${BORINGSSL_INCLUDE_DIR})
link_directories(${CRYPTO_LIB_DIR})
link_directories(${SSL_LIB_DIR})

# Build protobuf as an external project.
set(PROTOBUF_INSTALL_DIR ${CMAKE_BINARY_DIR}/third_party/protobuf)
set(PROTOBUF_INCLUDE_DIR ${PROTOBUF_INSTALL_DIR}/include)
set(PROTOBUF_LIB_DIR ${PROTOBUF_INSTALL_DIR}/lib)
set(PROTOC ${PROTOBUF_INSTALL_DIR}/bin/protoc)
ExternalProject_Add(protobuf_external_project
                    SOURCE_DIR  ${CMAKE_SOURCE_DIR}/third_party/protobuf
                    PREFIX      ${PROTOBUF_INSTALL_DIR}
                    INSTALL_DIR ${PROTOBUF_INSTALL_DIR}
                    CMAKE_ARGS  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                                -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
                                -DCMAKE_INSTALL_PREFIX:PATH=${PROTOBUF_INSTALL_DIR}
                                -Wno-sign-compare -Wno-ignored-qualifiers
                                -Wno-deprecated-register -Wno-unused-function
                                -Wno-unused-local-typedef
                                -Wno-missing-field-initializers
                                -Wno-enum-compare-switch
                   )
include_directories(BEFORE SYSTEM ${PROTOBUF_INCLUDE_DIR})
link_directories(${PROTOBUF_LIB_DIR})
# In the CMake build for protobuf we name the full protobuf library protobuf_full_cobalt.
set(PROTOBUF_FULL_LIB_NAME protobuf_full_cobalt)


# Build grpc as an external project.
set(GRPC_INSTALL_DIR ${CMAKE_BINARY_DIR}/third_party/grpc)
set(GRPC_INCLUDE_DIR ${GRPC_INSTALL_DIR}/include)
set(GRPC_LIB_DIR ${GRPC_INSTALL_DIR}/lib)
set(GRPC_CPP_PLUGIN ${GRPC_INSTALL_DIR}/bin/grpc_cpp_plugin)
ExternalProject_Add(grpc_external_project
                    SOURCE_DIR  ${CMAKE_SOURCE_DIR}/third_party/grpc
                    PREFIX      ${GRPC_INSTALL_DIR}
                    INSTALL_DIR ${GRPC_INSTALL_DIR}
                    CMAKE_ARGS  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                                -DCMAKE_INSTALL_PREFIX:PATH=${GRPC_INSTALL_DIR}
                                -DZLIB_ROOT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib
                                -DCARES_ROOT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/third_party/cares
                                -DBENCHMARK_ROOT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/third_party/benchmark
                                -DgRPC_PROTOBUF_PROVIDER=explicit
                                -DPROTOBUF_ROOT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/third_party/protobuf
                                -DgRPC_PROTOBUF_PROTOC=${PROTOC}
                                -DgRPC_PROTOBUF_PROTOC_LIB=protoc_lib
                                -DgRPC_PROTOBUF_LIB=protobuf_lite
                                -DgRPC_PROTOBUF_FULL_LIB=${PROTOBUF_FULL_LIB_NAME}
                                -DgRPC_PROTOBUF_LIB_DIR=${PROTOBUF_LIB_DIR}
                                -DgRPC_USE_PROTO_LITE=1
                                -DgRPC_SSL_PROVIDER=explicit
                                -DBORINGSSL_ROOT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/third_party/boringssl
                                -DgRPC_CRYPTO_LIBRARY=crypto
                                -DgRPC_SSL_LIBRARY=ssl
                                -DgRPC_CRYPTO_LIB_DIR=${CRYPTO_LIB_DIR}
                                -DgRPC_SSL_LIB_DIR=${SSL_LIB_DIR}
                                -DgRPC_GFLAGS_PROVIDER=explicit
                                -DgRPC_GFLAGS_LIB_DIR=${GFLAGS_LIB_DIR}
                                -Wno-sign-compare -Wno-ignored-qualifiers
                                -Wno-deprecated-register -Wno-unused-function
                                -Wno-unused-local-typedef
                                -Wno-implicit-function-declaration
                    DEPENDS     protobuf_external_project
                    DEPENDS     boringssl_external_project
                   )
include_directories(BEFORE SYSTEM ${GRPC_INCLUDE_DIR})
link_directories(${GRPC_LIB_DIR})
link_directories(${GRPC_INSTALL_DIR}/src/grpc_external_project-build/third_party/cares)

# Build googleapis as an external project.
set(GOOGLEAPIS_INSTALL_DIR ${CMAKE_BINARY_DIR}/third_party/googleapis)
set(GOOGLEAPIS_INCLUDE_DIR ${GOOGLEAPIS_INSTALL_DIR}/include)
set(GOOGLEAPIS_LIB_DIR ${GOOGLEAPIS_INSTALL_DIR}/lib)
ExternalProject_Add(googleapis_external_project
                    SOURCE_DIR  ${CMAKE_SOURCE_DIR}/third_party/googleapis
                    PREFIX      ${GOOGLEAPIS_INSTALL_DIR}
                    INSTALL_DIR ${GOOGLEAPIS_INSTALL_DIR}
                    CMAKE_ARGS  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                                -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
                                -DCMAKE_INSTALL_PREFIX:PATH=${GOOGLEAPIS_INSTALL_DIR}
                                -DPROTOC=${PROTOC}
                                -DPROTOBUF_INCLUDE_DIR=${PROTOBUF_INCLUDE_DIR}
                                -DGRPC_INCLUDE_DIR=${GRPC_INCLUDE_DIR}
                                -DPROTOBUF_LIB_DIR=${PROTOBUF_LIB_DIR}
                                -DGRPC_CPP_PLUGIN=${GRPC_CPP_PLUGIN}
                                -Wno-sign-compare -Wno-ignored-qualifiers
                                -Wno-deprecated-register -Wno-unused-function
                                -Wno-unused-local-typedef
                                -Wno-missing-field-initializers
                                -Wno-enum-compare-switch
                    DEPENDS     protobuf_external_project
                    DEPENDS     grpc_external_project
                   )
include_directories(BEFORE SYSTEM ${GOOGLEAPIS_INCLUDE_DIR})
link_directories(${GOOGLEAPIS_LIB_DIR})

# A target to combine all of the external projects.
add_custom_target(build_external_projects
                  DEPENDS gtest_external_project
                  DEPENDS gflags_external_project
                  DEPENDS glog_external_project
                  DEPENDS boringssl_external_project
                  DEPENDS protobuf_external_project
                  DEPENDS grpc_external_project
                  DEPENDS googleapis_external_project)

# Applying this macro to a target does two things:
# (i)  It causes build_external_projects to happen first before the target
#      is built.
# (ii) It adds link-time dependencies to the static libraries for all of
#      the third-party projects. You apply it as follows:
# add_base_dependencies(foo)
macro(add_base_dependencies target_name)
  add_dependencies(${target_name} build_external_projects)
  target_link_libraries(${target_name}
    # These are listed in dependency order. If a depends on b then
    # a should occur before b.
    grpc++
    grpc
    gpr
    ${PROTOBUF_FULL_LIB_NAME}
    glog
    gflags
    cares
    z
    ssl
    crypto
  )
endmacro()

# This macro should be applied to most library and exe targets in the cobalt
# build. Note that we separate out cobalt_proto_lib and config_proto_lib
# just so that we can apply the macro add_base_dependencies() when we
# build those two libraries. Every other library besides those two should
# depend on those two.
macro(add_cobalt_dependencies target_name)
  target_link_libraries(${target_name}
    cobalt_proto_lib
    config_proto_lib
  )
  add_base_dependencies(${target_name})
endmacro()

# Runs the protoc compiler on a set of .proto files to generate c++ files.
# Compiles the c++ files into a static library.
#
# Args:
# LIB_NAME: The name of the CMake target for the generated static library
# HDRS_OUT: A variable in which to write the list of names of the generated
#           header files
# USE_GRPC: A bool indicating whether or not use the gRPC plugin.
# <remaining args>: List of simple names of .proto files to include from the
#                   current source directory. The names should not include
#                   ".proto"
# example usage:
#
# cobalt_make_protobuf_cpp_lib(report_master_proto_lib
#                              REPORT_PROTO_HDRS
#                              true
#                              report_master report_internal)
#
# This will compile the files report_master.proto and report_internal.proto in
# the current source directory and generate a static library with a
# target name of report_master_proto_lib containing the ReportMaster gRPC
# service as well as the compiled protos from report_internal. The variable
# REPORT_PROTO_HDRS will contain the list of strings:
# { <some-path>/report_internal.grpc.pb.h
#   <some-path>/report_internal.pb.h
#   <some-path>/report_master.grpc.pb.h
#   <some-path>/report_master.pb.h}
# (Note that report_internal.proto does not contain a gRPC service definition
#  so that report_internal.grpc.pb.h is essentially empty.)
macro(cobalt_make_protobuf_cpp_lib LIB_NAME HDRS_OUT USE_GRPC)
  set(_protofiles)
  set(_generated_srcs)
  set(_generated_hdrs)
  foreach(name ${ARGN})
      list(APPEND _protofiles "${CMAKE_CURRENT_SOURCE_DIR}/${name}.proto")
      list(APPEND _generated_srcs "${CMAKE_CURRENT_BINARY_DIR}/${name}.pb.cc")
      list(APPEND _generated_hdrs "${CMAKE_CURRENT_BINARY_DIR}/${name}.pb.h")
      set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/${name}.pb.cc" PROPERTIES GENERATED TRUE)
      set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/${name}.pb.h" PROPERTIES GENERATED TRUE)
      if(${USE_GRPC})
        list(APPEND _generated_srcs "${CMAKE_CURRENT_BINARY_DIR}/${name}.grpc.pb.cc")
        list(APPEND _generated_hdrs "${CMAKE_CURRENT_BINARY_DIR}/${name}.grpc.pb.h")
        set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/${name}.grpc.pb.cc" PROPERTIES GENERATED TRUE)
        set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/${name}.grpc.pb.h" PROPERTIES GENERATED TRUE)
      endif()
  endforeach()
  set(_grpc_spec)
  if (${USE_GRPC})
    set(_grpc_spec
      --grpc_out=${CMAKE_BINARY_DIR}
      --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
    )
  endif()
  add_custom_command(OUTPUT ${_generated_srcs} ${_generated_hdrs}
    COMMAND ${PROTOC} ${_protofiles}
            -I ${CMAKE_SOURCE_DIR}
            -I ${PROTOBUF_INCLUDE_DIR}
            --cpp_out=${CMAKE_BINARY_DIR}
            ${_grpc_spec}
    DEPENDS ${_protofiles}
    DEPENDS build_external_projects
  )
  add_library(${LIB_NAME}
    ${_generated_srcs}
  )
  if(${USE_GRPC})
    # This is a bit of a hack. We want to add dependencies on
    # cobalt_proto_lib and config_proto_lib as long as those aren't the
    # library we are currently building. We know those two are not gRPC
    # libraries.
    add_cobalt_dependencies(${LIB_NAME})
  else()
    add_base_dependencies(${LIB_NAME})
  endif()
  set(${HDRS_OUT} ${_generated_hdrs})
endmacro()

# Runs the protoc compiler on a set of .proto files to generate proto buf
# descriptor files. Generates a custom target that may be used in an
# add_dependencies()
#
# Args:
# TARGET_NAME: The name of the generated CMake target.
# DESCRIPTORS_OUT: A variable in which to write the list of names of the
#                  generated descriptor files
# <remaining args>: List of simple names of .proto files to include from the
#                   current source directory. The names should not include
#                   ".proto"
#
# example usage:
#
# cobalt_generate_protobuf_descriptors(generate_report_master_descriptor
#                                      REPORT_MASTER_PROTO_DESCRIPTOR
#                                      report_master)
#
# This will compile the file report_master.proto in the current source dir
# and generate a descriptor for it in the current binary dir. A CMake target
# named generate_report_master_descriptor will be created on which other
# targets may depend. The variable REPORT_MASTER_PROTO_DESCRIPTOR will
# contain the list of strings:
# { <some-path>/report_master.descriptor }
macro(cobalt_generate_protobuf_descriptors TARGET_NAME DESCRIPTORS_OUT)
  set(_protofiles)
  set(_generated_dscrptrs)
  foreach(name ${ARGN})
      list(APPEND _protofiles "${CMAKE_CURRENT_SOURCE_DIR}/${name}.proto")
      list(APPEND _generated_dscrptrs "${CMAKE_CURRENT_BINARY_DIR}/${name}.descriptor")
      set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/${name}.descriptor" PROPERTIES GENERATED TRUE)
  endforeach()
  add_custom_command(OUTPUT ${_generated_dscrptrs}
    COMMAND ${PROTOC}
            -I ${CMAKE_SOURCE_DIR}
            -I ${PROTOBUF_INCLUDE_DIR}
            --include_imports
            --include_source_info ${_protofiles}
            --descriptor_set_out ${_generated_dscrptrs}
    DEPENDS ${_protofiles}
    DEPENDS build_external_projects
  )
  add_custom_target(${TARGET_NAME} ALL
    DEPENDS ${_generated_dscrptrs}
  )
  set(${DESCRIPTORS_OUT} ${_generated_dscrptrs})
endmacro()

# Runs the protoc compiler on a set of .proto files to generate go source files.
# Generates a custom target that may be used in an add_dependencies()
#
# Args:
# TARGET_NAME: The name of the generated CMake target.
# SRCS_OUT: A variable in which to write the list of names of the generated
#           go source files
# USE_GRPC: A bool indicating whether or not use the gRPC plugin.
# <remaining args>: List of simple names of .proto files to include from the
#                   current source directory. The names should not include
#                   ".proto"
# example usage:
#
# cobalt_protobuf_generate_go(generate_report_master_pb_go_files
#                             REPORT_MASTER_PB_GO_FILES
#                             true
#                             report_master)
#
# This will compile the file report_master.proto in the current source
# directory and generate a go source file in the appropriate directory
# under ${GO_PROTO_GEN_SRC_DIR}. A CMake target named
# generate_report_master_pb_go_files will be created on which other
# targets may depend. The variable SRCS_OUT will contain the list of strings:
# { <some-path>/report_master.pb.go }
macro(cobalt_protobuf_generate_go TARGET_NAME SRCS_OUT USE_GRPC)
  set(_gen_root_dir ${GO_PROTO_GEN_SRC_DIR})
  string(REPLACE ${CMAKE_BINARY_DIR} ${GO_PROTO_GEN_SRC_DIR} _gen_full_dir ${CMAKE_CURRENT_BINARY_DIR})
  string(COMPARE EQUAL ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR} is_root_dir)
  if(is_root_dir)
    # What's going on here? Some of our .proto files are in the root directory
    # but use "package cobalt". The Go compiler and the protoc compiler don't
    # play together well for these files. So we treat all of our top-level
    # proto files as if they were in a directory named "cobalt" and this
    # works well with go.
    string(CONCAT _gen_root_dir ${GO_PROTO_GEN_SRC_DIR} "/cobalt")
    set(_gen_full_dir ${_gen_root_dir})
  endif()
  set(_protofiles)
  set(_generated_srcs)
  foreach(name ${ARGN})
      list(APPEND _protofiles "${CMAKE_CURRENT_SOURCE_DIR}/${name}.proto")
      list(APPEND _generated_srcs "${_gen_full_dir}/${name}.pb.go")
      set_source_files_properties("${gen_full}/${name}.pb.go" PROPERTIES GENERATED TRUE)
  endforeach()
  set(_plugin_prefix "")
  if(${USE_GRPC})
    set(_plugin_prefix "grpc,")
  endif()
  add_custom_command(
    OUTPUT ${_generated_srcs}
    COMMAND ${PROTOC} ${_protofiles}
            -I ${CMAKE_SOURCE_DIR}
            -I ${PROTOBUF_INCLUDE_DIR}
            -I ${CMAKE_SOURCE_DIR}/third_party/go/src
            --go_out=plugins=${_plugin_prefix}Mobservation.proto=cobalt,Mencrypted_message.proto=cobalt:${_gen_root_dir}
    DEPENDS ${_protofiles}
    DEPENDS build_external_projects
  )
  add_custom_target(${TARGET_NAME}
    DEPENDS ${_generated_srcs}
  )
  set(${SRCS_OUT} ${_generated_srcs})
endmacro()

# Generate the C++ bindings for the Cobalt proto files in the root directory.
# Also compile the generated C++ files into a static library.
cobalt_make_protobuf_cpp_lib(cobalt_proto_lib
                             COBALT_PROTO_HDRS
                             false
                             encrypted_message
                             observation)

# Generate the go bindings for the Cobalt proto files in the root directory.
cobalt_protobuf_generate_go(generate_cobalt_pb_go_files
                            COBALT_PB_GO_FILES
                            false
                            encrypted_message
                            observation)

# Analagous targets to the two above also appear in the config directory for
# the config protos. But the variables that are generated there are not
# available in other sub-directories of the root directory. So we variables
# here that contain the same content.
set(CONFIG_PROTO_HDRS
    "${CMAKE_BINARY_DIR}/config/encodings.pb.h"
    "${CMAKE_BINARY_DIR}/config/metrics.pb.h"
    "${CMAKE_BINARY_DIR}/config/report_configs.pb.h")
set(CONFIG_PB_GO_FILES
    "${GO_PROTO_GEN_SRC_DIR}/config/encodings.pb.go"
    "${GO_PROTO_GEN_SRC_DIR}/config/metrics.pb.go"
    "${GO_PROTO_GEN_SRC_DIR}/config/reports.pb.go"
)

macro(declare_proto_files_are_generated)
  set_source_files_properties(${COBALT_PROTO_HDRS} PROPERTIES GENERATED TRUE)
  set_source_files_properties(${CONFIG_PROTO_HDRS} PROPERTIES GENERATED TRUE)
  set_source_files_properties(${COBALT_PB_GO_FILES} PROPERTIES GENERATED TRUE)
  set_source_files_properties(${CONFIG_PB_GO_FILES} PROPERTIES GENERATED TRUE)
endmacro()

macro(add_cobalt_test_dependencies target_name test_dir)
  target_link_libraries(${target_name}
    gtest gtest_main
  )
  add_cobalt_dependencies(${target_name})
  set_target_properties(${target_name}
      PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${test_dir})
endmacro()

# Third-party directories that we include using the simpler add_subdirectory method
include_directories(BEFORE SYSTEM "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lossmin")
add_subdirectory(third_party/lossmin/lossmin)

# Project directories
add_subdirectory(algorithms)
add_subdirectory(analyzer)
add_subdirectory(config)
add_subdirectory(encoder)
add_subdirectory(end_to_end_tests)
add_subdirectory(shuffler)
add_subdirectory(tools)
add_subdirectory(util)


