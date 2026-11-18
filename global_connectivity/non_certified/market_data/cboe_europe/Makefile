# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Cboe Europe Multicast PITCH handler (not certified): generated headers, the
# decode test and the handler's unit tests. Not wired into the top-level build.
#
#   export KSPRPROJ=~/kaspar-hft
#   make test                     # generate, build, run both test programs
#   make KSPRLIB=/path/to/lib test    # libchutil.a from a built tree
#
# The PITCH 2.X header is generated from cboe_europe.tbl; the unit header, GRP
# and Spin layers from ../cboe/pitch_us.tbl, which Cboe Europe shares
# (feed.md section 3). Both go to build/include/cboe_europe/gen; they are not
# committed.

KSPRPROJ ?= $(abspath $(CURDIR)/../../../..)
include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

KSPRLIB ?= $(KSPRPROJ)/lib
CXX ?= c++
PYTHON ?= python3
FIELDGEN = $(KSPRPROJ)/.claude/skills/build-feed-handler/tools/fieldgen.py
US_TBL = $(KSPRPROJ)/global_connectivity/non_certified/market_data/cboe/pitch_us.tbl

GEN = build/include/cboe_europe/gen
HEADERS = $(GEN)/cboe_europe.hpp $(GEN)/pitch_us.hpp

SRC = src/PitchEuHandler.cpp
TESTS = tests/test_pitch_eu_handler.cpp

FLAGS = $(STANDARD) -O1 -g -Wall -Wextra -Wno-deprecated-declarations -Wno-unused-parameter -Iinclude -Ibuild/include $(INCL) -I$(GTEST_PATH)/include
LIBS = -L$(KSPRLIB) -lchutil -L$(GTEST_PATH)/lib -lgtest -lgtest_main -L$(ZLIB_PATH)/lib -lz -lpthread

OBJ = $(patsubst %.cpp,build/obj/%.o,$(SRC) $(TESTS))

.PHONY: all headers check test clean
all: build/run_pitch_eu_tests build/cboe_europe_decode_test

check:
	$(PYTHON) $(FIELDGEN) cboe_europe.tbl --check

headers: $(HEADERS)

$(GEN)/cboe_europe.hpp: cboe_europe.tbl $(FIELDGEN)
	@mkdir -p $(GEN)
	$(PYTHON) $(FIELDGEN) cboe_europe.tbl --check
	$(PYTHON) $(FIELDGEN) cboe_europe.tbl -o $@

$(GEN)/pitch_us.hpp: $(US_TBL) $(FIELDGEN)
	@mkdir -p $(GEN)
	$(PYTHON) $(FIELDGEN) $(US_TBL) -o $@

build/cboe_europe_decode_test: tests/cboe_europe_decode_test.cpp $(HEADERS)
	$(CXX) -std=c++20 -Wall -Wextra -Werror -I$(GEN) $< -o $@

build/obj/%.o: %.cpp $(HEADERS) $(wildcard include/cboe_europe/*.hpp)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

build/run_pitch_eu_tests: $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LIBS)

test: all
	./build/cboe_europe_decode_test
	./build/run_pitch_eu_tests

clean:
	rm -rf build
