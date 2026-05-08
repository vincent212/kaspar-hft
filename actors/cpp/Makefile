# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
#
# Licensed under the MIT License. See LICENSE file in the project root.

LIBSRC = Actor.cpp Manager.cpp Group.cpp RustActorRefStub.cpp
# Registry client is needed by Manager and Group for remote actor lookup
REGISTRY_CLIENT_SRC = src/registry/RegistryClient.cpp
NAM = actors

# Coordination module sources
COORD_SRC = src/coordination/GroupManager.cpp \
            src/coordination/ZmqRouterReceiver.cpp \
            src/coordination/ZmqRouterSender.cpp \
            src/coordination/CoordinatorActor.cpp
COORD_MAIN = src/coordination/group_manager_main.cpp
COORD_ACTOR_MAIN = src/coordination/coordinator_actor_main.cpp

# Registry module sources
REGISTRY_SRC = src/registry/RegistryClient.cpp src/registry/RegistryQueryActor.cpp src/registry/RegistryActor.cpp
REGISTRY_MAIN = src/registry/global_registry_main.cpp
REGISTRY_ACTOR_MAIN = src/registry/registry_actor_main.cpp

# Console/Monitoring module sources
CONSOLE_SRC = src/console/Table.cpp src/console/ConsoleActor.cpp src/console/MQ0ServerActor.cpp

CXX = g++

# OS detection
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    BOOST_PATH = /opt/homebrew/opt/boost
    CPPZMQ_PATH = /opt/homebrew/opt/cppzmq
    ZMQ_PATH = /opt/homebrew/opt/zeromq
    JSON_PATH = /opt/homebrew/opt/nlohmann-json
    GTEST_PATH = /opt/homebrew/opt/googletest
else
    BOOST_PATH = /home/vmayeski/local/boost190
    CPPZMQ_PATH = /usr/local
    ZMQ_PATH = /usr/local
    JSON_PATH = /home/vmayeski/local
    GTEST_PATH = /usr/local
endif

CXXFLAGS_COMMON = -std=c++20 -Iinclude -I../../chutil/include -I$(BOOST_PATH)/include -I$(CPPZMQ_PATH)/include -I$(ZMQ_PATH)/include -I$(JSON_PATH)/include -I$(GTEST_PATH)/include
CXXFLAGS_OPT = $(CXXFLAGS_COMMON) -O3 -march=native -fPIC
CXXFLAGS_DBG = $(CXXFLAGS_COMMON) -O0 -g -DDEBUG -fPIC
LDFLAGS = -lpthread

# Remote actor support (ZMQ + JSON)
REMOTE_LDFLAGS = -L$(ZMQ_PATH)/lib -lzmq

OBJDIRO = obj
OBJDIRG = objg
OBJS_OPT = $(patsubst %.cpp,$(OBJDIRO)/%.o,$(LIBSRC))
OBJS_DBG = $(patsubst %.cpp,$(OBJDIRG)/%.o,$(LIBSRC))
COORD_OBJS_OPT = $(patsubst %.cpp,$(OBJDIRO)/%.o,$(COORD_SRC))
REGISTRY_OBJS_OPT = $(patsubst %.cpp,$(OBJDIRO)/%.o,$(REGISTRY_SRC))
CONSOLE_OBJS_OPT = $(patsubst %.cpp,$(OBJDIRO)/%.o,$(CONSOLE_SRC))
CONSOLE_OBJS_DBG = $(patsubst %.cpp,$(OBJDIRG)/%.o,$(CONSOLE_SRC))
LIB = lib$(NAM).a
LIBG = lib$(NAM)g.a

all: opt

opt: $(LIB)

debug: $(LIBG)

# RegistryClient object file for library
REGISTRY_CLIENT_OBJ_OPT = $(OBJDIRO)/src/registry/RegistryClient.o
REGISTRY_CLIENT_OBJ_DBG = $(OBJDIRG)/src/registry/RegistryClient.o

$(LIB): $(OBJS_OPT) $(REGISTRY_CLIENT_OBJ_OPT) $(CONSOLE_OBJS_OPT)
	ar rcs $@ $^

$(LIBG): $(OBJS_DBG) $(REGISTRY_CLIENT_OBJ_DBG) $(CONSOLE_OBJS_DBG)
	ar rcs $@ $^

$(OBJDIRO)/%.o: %.cpp | $(OBJDIRO)
	$(CXX) $(CXXFLAGS_OPT) -c $< -o $@

$(OBJDIRG)/%.o: %.cpp | $(OBJDIRG)
	$(CXX) $(CXXFLAGS_DBG) -c $< -o $@

$(OBJDIRO):
	@mkdir -p $(OBJDIRO)
	@mkdir -p $(OBJDIRO)/src/registry
	@mkdir -p $(OBJDIRO)/src/coordination
	@mkdir -p $(OBJDIRO)/src/console

$(OBJDIRG):
	@mkdir -p $(OBJDIRG)
	@mkdir -p $(OBJDIRG)/src/registry
	@mkdir -p $(OBJDIRG)/src/coordination
	@mkdir -p $(OBJDIRG)/src/console

# Coordination object files
$(OBJDIRO)/src/coordination/%.o: src/coordination/%.cpp | $(OBJDIRO)
	@mkdir -p $(OBJDIRO)/src/coordination
	$(CXX) $(CXXFLAGS_OPT) -c $< -o $@

# Coordination executables
COORD_ACTOR_BIN = coordinator_actor

coordination-actor: $(COORD_ACTOR_BIN)

$(COORD_ACTOR_BIN): $(COORD_OBJS_OPT) $(COORD_ACTOR_MAIN) $(LIB)
	$(CXX) $(CXXFLAGS_OPT) $(COORD_ACTOR_MAIN) $(COORD_OBJS_OPT) -o $@ -L. -l$(NAM) $(LDFLAGS) $(REMOTE_LDFLAGS)

# Registry object files (optimized)
$(OBJDIRO)/src/registry/%.o: src/registry/%.cpp | $(OBJDIRO)
	@mkdir -p $(OBJDIRO)/src/registry
	$(CXX) $(CXXFLAGS_OPT) -c $< -o $@

# Registry object files (debug)
$(OBJDIRG)/src/registry/%.o: src/registry/%.cpp | $(OBJDIRG)
	@mkdir -p $(OBJDIRG)/src/registry
	$(CXX) $(CXXFLAGS_DBG) -c $< -o $@

# Console object files (optimized)
$(OBJDIRO)/src/console/%.o: src/console/%.cpp | $(OBJDIRO)
	@mkdir -p $(OBJDIRO)/src/console
	$(CXX) $(CXXFLAGS_OPT) -c $< -o $@

# Console object files (debug)
$(OBJDIRG)/src/console/%.o: src/console/%.cpp | $(OBJDIRG)
	@mkdir -p $(OBJDIRG)/src/console
	$(CXX) $(CXXFLAGS_DBG) -c $< -o $@

# Registry executable (Actor-based version)
REGISTRY_BIN = global_registry

registry: $(REGISTRY_BIN)

$(REGISTRY_BIN): $(REGISTRY_OBJS_OPT) $(CONSOLE_OBJS_OPT) $(COORD_OBJS_OPT) $(REGISTRY_ACTOR_MAIN) $(LIB)
	$(CXX) $(CXXFLAGS_OPT) $(REGISTRY_ACTOR_MAIN) $(REGISTRY_OBJS_OPT) $(CONSOLE_OBJS_OPT) $(COORD_OBJS_OPT) -o $@ -L. -l$(NAM) $(LDFLAGS) $(REMOTE_LDFLAGS)

# Example targets (registry examples are in ~/actors/registry/examples/)
examples: examples/ping_pong examples/remote_pong examples/remote_ping

examples/ping_pong: examples/ping_pong.cpp $(LIB)
	$(CXX) $(CXXFLAGS_OPT) $< -o $@ -L. -l$(NAM) $(LDFLAGS) $(REMOTE_LDFLAGS)

examples/remote_pong: examples/remote_pong.cpp $(LIB)
	$(CXX) $(CXXFLAGS_OPT) $< -o $@ -L. -l$(NAM) $(LDFLAGS) $(REMOTE_LDFLAGS)

examples/remote_ping: examples/remote_ping.cpp $(LIB)
	$(CXX) $(CXXFLAGS_OPT) $< -o $@ -L. -l$(NAM) $(LDFLAGS) $(REMOTE_LDFLAGS)

# Test targets (requires Google Test)
TEST_SRC = $(wildcard tests/test_*.cpp)
TEST_BIN = run_tests

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRC) $(LIB)
	$(CXX) $(CXXFLAGS_OPT) $(TEST_SRC) -o $@ -L. -L$(GTEST_PATH)/lib -l$(NAM) $(LDFLAGS) $(REMOTE_LDFLAGS) -lgtest -lgtest_main

# Coordination tests
COORD_TEST_SRC = $(wildcard tests/coordination/test_*.cpp)
COORD_TEST_BIN = run_coord_tests

test-coordination: $(COORD_TEST_BIN)
	./$(COORD_TEST_BIN)

$(COORD_TEST_BIN): $(COORD_TEST_SRC) $(COORD_OBJS_OPT) $(REGISTRY_OBJS_OPT) $(LIB)
	$(CXX) $(CXXFLAGS_OPT) $(COORD_TEST_SRC) $(COORD_OBJS_OPT) $(REGISTRY_OBJS_OPT) -o $@ -L. -L$(GTEST_PATH)/lib -l$(NAM) $(LDFLAGS) $(REMOTE_LDFLAGS) -lgtest -lgtest_main

# Registry tests
REGISTRY_TEST_SRC = $(wildcard tests/registry/test_*.cpp)
REGISTRY_TEST_BIN = run_registry_tests

test-registry: $(REGISTRY_TEST_BIN)
	./$(REGISTRY_TEST_BIN)

$(REGISTRY_TEST_BIN): $(REGISTRY_TEST_SRC) $(REGISTRY_OBJS_OPT) $(LIB)
	$(CXX) $(CXXFLAGS_OPT) $(REGISTRY_TEST_SRC) $(REGISTRY_OBJS_OPT) -o $@ -L. -L$(GTEST_PATH)/lib -l$(NAM) $(LDFLAGS) $(REMOTE_LDFLAGS) -lgtest -lgtest_main

# Monitoring tests
MONITORING_TEST_SRC = tests/test_monitoring.cpp
MONITORING_TEST_BIN = run_monitoring_tests

test-monitoring: $(MONITORING_TEST_BIN)
	./$(MONITORING_TEST_BIN)

$(MONITORING_TEST_BIN): $(MONITORING_TEST_SRC) $(LIB)
	$(CXX) $(CXXFLAGS_OPT) $(MONITORING_TEST_SRC) -o $@ -L. -L$(GTEST_PATH)/lib -l$(NAM) $(LDFLAGS) $(REMOTE_LDFLAGS) -lgtest -lgtest_main

clean:
	rm -rf $(OBJDIRO) $(OBJDIRG) $(LIB) $(LIBG) $(TEST_BIN) $(COORD_ACTOR_BIN) $(COORD_TEST_BIN) $(REGISTRY_BIN) $(REGISTRY_TEST_BIN) $(MONITORING_TEST_BIN) examples/ping_pong examples/remote_pong examples/remote_ping

.PHONY: all opt debug clean examples test coordination-actor test-coordination registry test-registry test-monitoring
