# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# FPGA actors. No Vitis and no card needed: the FPGA design runs in software.
#   make run       build and run both examples
#   make check14   compile the FPGA side as C++14, the dialect Vitis HLS accepts

CXX = g++
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  ZMQ_PATH ?= /opt/homebrew/opt/zeromq
else
  ZMQ_PATH ?= /usr/local
endif

WARN = -Wall -Wextra -Werror -Wno-unknown-pragmas
CXXFLAGS = -std=c++17 -O2 -g $(WARN) -Iinclude -Iexamples/ping_pong

# The CPU side uses the actors/cpp runtime (the FpgaLanes mailbox).
ACTORS_CPP = ../cpp
HOST_FLAGS = $(CXXFLAGS) -Ihost -I$(ACTORS_CPP)/include
HOST_LIBS = -L$(ACTORS_CPP) -lactors -L$(ZMQ_PATH)/lib -lzmq -lpthread $(RPATH)

# Run against the compiler's own libstdc++, not an older system one.
RPATH = -Wl,-rpath,$(dir $(realpath $(shell $(CXX) -print-file-name=libstdc++.so)))

KFPGA = $(wildcard include/kfpga/*.hpp) $(wildcard examples/ping_pong/*.hpp)

.PHONY: run check14 clean
run: check14 fpga_to_fpga cpu_fpga
	./fpga_to_fpga
	./cpu_fpga

# actors/cpp does not track header dependencies: rebuild it whole (-B) when
# any of its headers or sources is newer than the library.
$(ACTORS_CPP)/libactors.a: $(wildcard $(ACTORS_CPP)/include/actors/*.hpp) $(wildcard $(ACTORS_CPP)/*.cpp)
	$(MAKE) -B -C $(ACTORS_CPP) libactors.a

fpga_to_fpga: examples/ping_pong/fpga_to_fpga.cpp $(KFPGA)
	$(CXX) $(CXXFLAGS) $< -o $@ -lpthread $(RPATH)

cpu_fpga: examples/cpu_fpga/cpu_fpga.cpp examples/cpu_fpga/*.hpp host/kfpga/*.hpp $(KFPGA) $(ACTORS_CPP)/libactors.a
	$(CXX) $(HOST_FLAGS) -Iexamples/cpu_fpga $< -o $@ $(HOST_LIBS)

check14:
	echo '#include "fpga_actors.hpp"' | $(CXX) -std=c++14 $(WARN) -Iinclude -Iexamples/ping_pong -fsyntax-only -x c++ -

clean:
	rm -f fpga_to_fpga cpu_fpga
