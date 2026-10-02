# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# FPGA actor runtime.
#   make test      unit tests on the host (plain C++14, no Vitis needed)
#   make synth     Vitis HLS synthesis of the runtime's parts: latency in cycles
#                  (Linux with Vitis HLS; see hls/run_hls.tcl)

CXX = g++
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  GTEST_PATH ?= /opt/homebrew/opt/googletest
  BOOST_PATH ?= /opt/homebrew/opt/boost
  ZMQ_PATH   ?= /opt/homebrew/opt/zeromq
else
  GTEST_PATH ?= /usr
  BOOST_PATH ?= /usr/local
  ZMQ_PATH   ?= /usr/local
endif

# The CPU side (bridge, CPU actors) uses the actors/cpp runtime.
ACTORS_CPP = ../cpp
HOST_FLAGS = -std=c++17 -O2 $(WARN) -Iinclude -Ihost -I$(ACTORS_CPP)/include -I$(BOOST_PATH)/include
HOST_LIBS  = -L$(ACTORS_CPP) -lactors -L$(ZMQ_PATH)/lib -lzmq -lpthread

# The runtime is kept to C++14, the dialect Vitis HLS accepts: `make check14`
# compiles the headers and the design top as C++14. GoogleTest needs C++17, so the
# tests themselves build as C++17.
WARN = -Wall -Wextra -Werror -Wno-unknown-pragmas
CXXFLAGS = -std=c++17 -O2 $(WARN) -Iinclude

.PHONY: test check14 synth clean example
test: check14 run_tests run_bridge_tests
	./run_tests
	./run_bridge_tests

# actors/cpp does not track header dependencies, so rebuild the whole library
# (-B) whenever any of its headers or sources is newer than it: the FPGA bridge
# adds an alternative to ActorRef, and a library built without it must not be
# linked.
$(ACTORS_CPP)/libactors.a: $(wildcard $(ACTORS_CPP)/include/actors/*.hpp) $(wildcard $(ACTORS_CPP)/*.cpp)
	$(MAKE) -B -C $(ACTORS_CPP) libactors.a

# CPU <-> FPGA: the bridge and the four-direction example.
run_bridge_tests: tests/test_bridge.cpp host/*.hpp include/actors_fpga/*.hpp examples/cpu_fpga/*.hpp $(ACTORS_CPP)/libactors.a
	$(CXX) $(HOST_FLAGS) -I$(GTEST_PATH)/include -Iexamples/cpu_fpga tests/test_bridge.cpp -o $@ \
	  $(HOST_LIBS) -L$(GTEST_PATH)/lib -lgtest -lgtest_main

cpu_fpga: examples/cpu_fpga/main.cpp examples/cpu_fpga/*.hpp host/*.hpp include/actors_fpga/*.hpp $(ACTORS_CPP)/libactors.a
	$(CXX) $(HOST_FLAGS) -Iexamples/cpu_fpga examples/cpu_fpga/main.cpp -o $@ $(HOST_LIBS)

example: cpu_fpga
	./cpu_fpga

check14:
	$(CXX) -std=c++14 $(WARN) -Iinclude -Iexamples/ping_pong -fsyntax-only hls/pingpong_top.cpp
	$(CXX) -std=c++14 $(WARN) -Iinclude -Iexamples/ping_pong -fsyntax-only hls/bench_tops.cpp

run_tests: tests/test_runtime.cpp include/actors_fpga/*.hpp examples/ping_pong/*.hpp
	$(CXX) $(CXXFLAGS) -I$(GTEST_PATH)/include tests/test_runtime.cpp -o $@ \
	  -L$(GTEST_PATH)/lib -lgtest -lgtest_main -lpthread

synth:
	cd hls && vitis_hls -f run_hls.tcl

clean:
	rm -rf run_tests run_bridge_tests cpu_fpga hls/kfpga_hls hls/*.log
