# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Actor-framework microbenchmarks. Each bench_*.cpp builds to its own binary and
# links the actors static library. New benches are picked up automatically.
#
#   KSPRPROJ=/path/to/kaspar-hft make        # build all benches
#   KSPRPROJ=/path/to/kaspar-hft make run     # build + run each bench
#   make clean

KSPRPROJ ?= ../../..
ACTORS   := $(KSPRPROJ)/actors/cpp
CXX      ?= g++

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    BOOST_PATH  ?= /opt/homebrew/opt/boost
    CPPZMQ_PATH ?= /opt/homebrew/opt/cppzmq
    ZMQ_PATH    ?= /opt/homebrew/opt/zeromq
    JSON_PATH   ?= /opt/homebrew/opt/nlohmann-json
else
    BOOST_PATH  ?= /usr/local
    CPPZMQ_PATH ?= /usr/local
    ZMQ_PATH    ?= /usr/local
    JSON_PATH   ?= /usr/local
endif

CXXFLAGS = -std=c++20 -O3 -march=native -W -Wall -Wextra \
           -I. -I$(ACTORS)/include -I$(KSPRPROJ)/chutil/include \
           -I$(BOOST_PATH)/include -I$(CPPZMQ_PATH)/include \
           -I$(ZMQ_PATH)/include -I$(JSON_PATH)/include

LDFLAGS = -L$(ACTORS) -lactors -lpthread -L$(ZMQ_PATH)/lib -lzmq

LIB      := $(ACTORS)/libactors.a
BENCHES  := $(patsubst %.cpp,%,$(wildcard bench_*.cpp))

# The MemoryPool is toggled by the compile-time DISABLE_MEMORY_POOL define, so
# bench_pingpong is also built with the pool OFF (bench_pingpong_nopool) to show
# the allocator's effect on the same pooled message types.
NOPOOL   := bench_pingpong_nopool

all: $(BENCHES) $(NOPOOL)

# The benches need the actors static library; build it if missing. NOTE: this
# does NOT track the actors sources -- if you change the framework, run
# `make lib` (or `make -C $(ACTORS)`) to rebuild it, or the benches will link a
# stale library and measure the old code.
$(LIB):
	$(MAKE) -C $(ACTORS) KSPRPROJ=$(KSPRPROJ)

# Force a rebuild of the actors library (use after editing the framework).
lib:
	$(MAKE) -C $(ACTORS) KSPRPROJ=$(KSPRPROJ)

bench_%: bench_%.cpp bench_common.hpp $(LIB)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

$(NOPOOL): bench_pingpong.cpp bench_common.hpp $(LIB)
	$(CXX) $(CXXFLAGS) -DDISABLE_MEMORY_POOL $< -o $@ $(LDFLAGS)

run: all
	@for b in $(BENCHES) $(NOPOOL); do echo "==> ./$$b"; ./$$b || exit 1; done

clean:
	rm -f $(BENCHES) $(NOPOOL)

.PHONY: all run clean lib
