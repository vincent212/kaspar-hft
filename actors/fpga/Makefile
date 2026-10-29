# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# FPGA actors.
#   make run       build and run the examples in software (SoftCard; no Vitis)
#   make check14   compile the FPGA side as C++14, the dialect Vitis HLS accepts
#   make hls       each example through Vitis HLS: C simulation, synthesis and
#                  RTL co-simulation (needs Vitis: source <Vitis>/settings64.sh)

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

.PHONY: run check14 check_group_fast_send hls hls_csim hls_synth hls_cosim clean
run: check14 check_group_fast_send fpga_to_fpga group cpu_fpga
	./fpga_to_fpga
	./group
	./cpu_fpga

# actors/cpp does not track header dependencies: rebuild it whole (-B) when
# any of its headers or sources is newer than the library.
$(ACTORS_CPP)/libactors.a: $(wildcard $(ACTORS_CPP)/include/actors/*.hpp) $(wildcard $(ACTORS_CPP)/*.cpp)
	$(MAKE) -B -C $(ACTORS_CPP) libactors.a

fpga_to_fpga: examples/ping_pong/fpga_to_fpga.cpp $(KFPGA)
	$(CXX) $(CXXFLAGS) $< -o $@ -lpthread $(RPATH)

group: examples/ping_pong/group.cpp $(KFPGA)
	$(CXX) $(CXXFLAGS) $< -o $@ -lpthread $(RPATH)

# fast_send only within a group: moving Pong out of SyncPing's group must not compile.
check_group_fast_send:
	@if $(CXX) $(CXXFLAGS) -DPONG_IN_OTHER_GROUP -fsyntax-only examples/ping_pong/group.cpp 2>check_group_fast_send.log; then \
	  echo "FAILED: fast_send to another group compiled"; exit 1; fi
	@grep -q "fast_send only within a group" check_group_fast_send.log && rm -f check_group_fast_send.log && \
	  echo "OK: fast_send to an actor in another group does not compile"

cpu_fpga: examples/cpu_fpga/cpu_fpga.cpp examples/cpu_fpga/*.hpp host/kfpga/*.hpp $(KFPGA) $(ACTORS_CPP)/libactors.a
	$(CXX) $(HOST_FLAGS) -Iexamples/cpu_fpga $< -o $@ $(HOST_LIBS)

check14:
	echo '#include "fpga_actors.hpp"' | $(CXX) -std=c++14 $(WARN) -Iinclude -Iexamples/ping_pong -fsyntax-only -x c++ -

# Vitis HLS. The part and clock are in hls/<design>.cfg; work goes to
# hls/w_<design>/, logs to hls/<design>.<step>.log. Kept off cores 0 and 15
# and at low priority: this box also runs the market-data recorders.
HLS_DESIGNS = fpga_to_fpga group
HLS_RUN = nice -n 19 taskset -c 1-12

define hls_step
	@for d in $(HLS_DESIGNS); do \
	  echo "== $$d: $(1)"; \
	  (cd hls && $(HLS_RUN) $(2) --config $$d.cfg --work_dir w_$$d > $$d.$(1).log 2>&1) || \
	    { echo "FAILED: see hls/$$d.$(1).log"; exit 1; }; \
	  grep -hE 'PASS|FAIL|Estimated Fmax|co-simulation finished' hls/$$d.$(1).log | sort -u; \
	done
endef

hls: hls_csim hls_synth hls_cosim
hls_csim:
	$(call hls_step,csim,vitis-run --mode hls --csim)
hls_synth:
	$(call hls_step,synth,v++ -c --mode hls)
hls_cosim: hls_synth
	$(call hls_step,cosim,vitis-run --mode hls --cosim)

clean:
	rm -f fpga_to_fpga group cpu_fpga check_group_fast_send.log
	rm -rf hls/w_* hls/*.log hls/.Xil
