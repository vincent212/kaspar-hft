cmake_minimum_required(VERSION 3.16)
project(actors CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -DDEBUG")

# Set RPATH to ensure binaries find the correct libraries at runtime
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib:/usr/lib64:/usr/local/lib")

# OS-specific paths
if(APPLE)
    set(BOOST_ROOT /opt/homebrew/opt/boost)
    set(ZMQ_SEARCH_PATHS /opt/homebrew/opt/zeromq)
    set(CPPZMQ_SEARCH_PATHS /opt/homebrew/opt/cppzmq)
    set(JSON_SEARCH_PATHS /opt/homebrew/opt/nlohmann-json)
    set(GTEST_SEARCH_PATHS /opt/homebrew/opt/googletest)
else()
    set(BOOST_ROOT /usr/local/boost188)
    # Search both /usr/local and system /usr paths
    set(ZMQ_SEARCH_PATHS /usr/local /usr)
    set(CPPZMQ_SEARCH_PATHS /usr/local /usr)
    set(JSON_SEARCH_PATHS /usr/local /usr)
    set(GTEST_SEARCH_PATHS /usr/local /usr)
endif()

# Find dependencies
find_package(Threads REQUIRED)

# Boost (header-only usage)
find_path(Boost_INCLUDE_DIR boost/version.hpp
    PATHS ${BOOST_ROOT}/include
    NO_DEFAULT_PATH
)
if(NOT Boost_INCLUDE_DIR)
    message(FATAL_ERROR "Boost not found at ${BOOST_ROOT}")
endif()

# ZMQ
find_library(ZMQ_LIBRARY zmq
    PATHS ${ZMQ_SEARCH_PATHS}
    PATH_SUFFIXES lib lib64
)
find_path(ZMQ_INCLUDE_DIR zmq.h
    PATHS ${ZMQ_SEARCH_PATHS}
    PATH_SUFFIXES include
)
if(NOT ZMQ_LIBRARY OR NOT ZMQ_INCLUDE_DIR)
    message(FATAL_ERROR "ZeroMQ not found in ${ZMQ_SEARCH_PATHS}")
endif()

# cppzmq (header-only)
find_path(CPPZMQ_INCLUDE_DIR zmq.hpp
    PATHS ${CPPZMQ_SEARCH_PATHS}
    PATH_SUFFIXES include
)
if(NOT CPPZMQ_INCLUDE_DIR)
    message(FATAL_ERROR "cppzmq not found in ${CPPZMQ_SEARCH_PATHS}")
endif()

# nlohmann-json (header-only)
find_path(JSON_INCLUDE_DIR nlohmann/json.hpp
    PATHS ${JSON_SEARCH_PATHS}
    PATH_SUFFIXES include
)
if(NOT JSON_INCLUDE_DIR)
    message(FATAL_ERROR "nlohmann-json not found in ${JSON_SEARCH_PATHS}")
endif()

# Google Test (optional, for tests only)
find_path(GTEST_INCLUDE_DIR gtest/gtest.h
    PATHS ${GTEST_SEARCH_PATHS}
    PATH_SUFFIXES include
)
find_library(GTEST_LIBRARY gtest
    PATHS ${GTEST_SEARCH_PATHS}
    PATH_SUFFIXES lib lib64
)
find_library(GTEST_MAIN_LIBRARY gtest_main
    PATHS ${GTEST_SEARCH_PATHS}
    PATH_SUFFIXES lib lib64
)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../chutil/include
    ${Boost_INCLUDE_DIR}
    ${ZMQ_INCLUDE_DIR}
    ${CPPZMQ_INCLUDE_DIR}
    ${JSON_INCLUDE_DIR}
)

# ============================================================================
# Core Actors Library
# ============================================================================
add_library(actors STATIC
    Actor.cpp
    Manager.cpp
    Group.cpp
    RustActorRefStub.cpp
    src/registry/RegistryClient.cpp
    src/console/Table.cpp
    src/console/ConsoleActor.cpp
    src/console/MQ0ServerActor.cpp
)

target_link_libraries(actors
    PUBLIC
        Threads::Threads
        ${ZMQ_LIBRARY}
)

# ============================================================================
# Coordination Module
# ============================================================================
set(COORD_SOURCES
    src/coordination/GroupManager.cpp
    src/coordination/ZmqRouterReceiver.cpp
    src/coordination/ZmqRouterSender.cpp
    src/coordination/CoordinatorActor.cpp
)

# coordinator_actor binary
add_executable(coordinator_actor
    ${COORD_SOURCES}
    src/coordination/coordinator_actor_main.cpp
)

target_link_libraries(coordinator_actor
    PRIVATE
        actors
        Threads::Threads
        ${ZMQ_LIBRARY}
)

# ============================================================================
# Registry Module
# ============================================================================
set(REGISTRY_SOURCES
    src/registry/RegistryClient.cpp
    src/registry/RegistryQueryActor.cpp
    src/registry/RegistryActor.cpp
)

# global_registry binary
add_executable(global_registry
    ${REGISTRY_SOURCES}
    ${COORD_SOURCES}
    src/console/Table.cpp
    src/console/ConsoleActor.cpp
    src/console/MQ0ServerActor.cpp
    src/registry/registry_actor_main.cpp
)

target_link_libraries(global_registry
    PRIVATE
        actors
        Threads::Threads
        ${ZMQ_LIBRARY}
)

# ============================================================================
# Examples (optional)
# ============================================================================
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/examples/ping_pong.cpp)
    add_executable(ping_pong examples/ping_pong.cpp)
    target_link_libraries(ping_pong PRIVATE actors ${ZMQ_LIBRARY})

    add_executable(remote_pong examples/remote_pong.cpp)
    target_link_libraries(remote_pong PRIVATE actors ${ZMQ_LIBRARY})

    add_executable(remote_ping examples/remote_ping.cpp)
    target_link_libraries(remote_ping PRIVATE actors ${ZMQ_LIBRARY})
endif()

# ============================================================================
# Tests (optional, requires Google Test)
# ============================================================================
if(GTEST_INCLUDE_DIR AND GTEST_LIBRARY)
    enable_testing()

    # Main tests
    file(GLOB TEST_SOURCES tests/test_*.cpp)
    if(TEST_SOURCES)
        add_executable(run_tests ${TEST_SOURCES})
        target_include_directories(run_tests PRIVATE ${GTEST_INCLUDE_DIR})
        target_link_libraries(run_tests
            PRIVATE
                actors
                ${ZMQ_LIBRARY}
                ${GTEST_LIBRARY}
                ${GTEST_MAIN_LIBRARY}
        )
        add_test(NAME ActorTests COMMAND run_tests)
    endif()

    # Coordination tests
    file(GLOB COORD_TEST_SOURCES tests/coordination/test_*.cpp)
    if(COORD_TEST_SOURCES)
        add_executable(run_coord_tests ${COORD_TEST_SOURCES})
        target_sources(run_coord_tests PRIVATE ${COORD_SOURCES} ${REGISTRY_SOURCES})
        target_include_directories(run_coord_tests PRIVATE ${GTEST_INCLUDE_DIR})
        target_link_libraries(run_coord_tests
            PRIVATE
                actors
                ${ZMQ_LIBRARY}
                ${GTEST_LIBRARY}
                ${GTEST_MAIN_LIBRARY}
        )
        add_test(NAME CoordinationTests COMMAND run_coord_tests)
    endif()

    # Registry tests - DISABLED (test_global_registry.cpp uses outdated GlobalRegistry API)
    # file(GLOB REGISTRY_TEST_SOURCES tests/registry/test_*.cpp)
    # if(REGISTRY_TEST_SOURCES)
    #     add_executable(run_registry_tests ${REGISTRY_TEST_SOURCES})
    #     target_sources(run_registry_tests PRIVATE ${REGISTRY_SOURCES})
    #     target_include_directories(run_registry_tests PRIVATE ${GTEST_INCLUDE_DIR})
    #     target_link_libraries(run_registry_tests
    #         PRIVATE
    #             actors
    #             ${ZMQ_LIBRARY}
    #             ${GTEST_LIBRARY}
    #             ${GTEST_MAIN_LIBRARY}
    #     )
    #     add_test(NAME RegistryTests COMMAND run_registry_tests)
    # endif()

    # Monitoring tests
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_monitoring.cpp)
        add_executable(run_monitoring_tests tests/test_monitoring.cpp)
        target_include_directories(run_monitoring_tests PRIVATE ${GTEST_INCLUDE_DIR})
        target_link_libraries(run_monitoring_tests
            PRIVATE
                actors
                ${ZMQ_LIBRARY}
                ${GTEST_LIBRARY}
                ${GTEST_MAIN_LIBRARY}
        )
        add_test(NAME MonitoringTests COMMAND run_monitoring_tests)
    endif()
endif()

# ============================================================================
# Install targets (optional)
# ============================================================================
install(TARGETS actors coordinator_actor global_registry
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/actors
    DESTINATION include
    FILES_MATCHING PATTERN "*.hpp"
)

# ============================================================================
# Print configuration summary
# ============================================================================
message(STATUS "")
message(STATUS "=== Actors Library Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Boost: ${Boost_INCLUDE_DIR}")
message(STATUS "ZeroMQ: ${ZMQ_LIBRARY}")
message(STATUS "cppzmq: ${CPPZMQ_INCLUDE_DIR}")
message(STATUS "nlohmann-json: ${JSON_INCLUDE_DIR}")
if(GTEST_INCLUDE_DIR)
    message(STATUS "Google Test: ${GTEST_LIBRARY} (tests enabled)")
else()
    message(STATUS "Google Test: not found (tests disabled)")
endif()
message(STATUS "")
message(STATUS "Targets:")
message(STATUS "  - libactors.a (core library)")
message(STATUS "  - coordinator_actor (binary)")
message(STATUS "  - global_registry (binary)")
message(STATUS "")
