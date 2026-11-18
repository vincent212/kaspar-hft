# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Nasdaq TotalView-ITCH 5.0 market-data handler (not certified). Not wired
# into the top-level build. Needs the kaspar libraries (frame, logger, mq0,
# chutil, mcast_recv, actors), Boost, ZeroMQ, zlib and Google Test:
#
#   export KSPRPROJ=~/kaspar-hft
#   make test                      # generate, build, run the unit tests
#   make replay                    # build tools: nasdaq_replay, itch_filter
#   make decode_test               # the field tables' decode test
#   make KSPRLIB=/path/to/lib ACTORS_LIB=/path/to/libactors.a test
#
# The ITCH, MoldUDP64, SoupBinTCP and GLIMPSE headers are generated from the
# *.tbl files into build/gen; they are not committed.

KSPRPROJ ?= $(abspath $(CURDIR)/../../../..)
include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

KSPRLIB ?= $(KSPRPROJ)/lib
ACTORS_LIB ?= $(KSPRPROJ)/actors/cpp/libactors.a
CXX ?= c++
PYTHON ?= python3
FIELDGEN = $(KSPRPROJ)/.claude/skills/build-feed-handler/tools/fieldgen.py

GEN = build/gen
TABLES = itch50 moldudp64 soupbintcp glimpse50
HEADERS = $(patsubst %,$(GEN)/%.hpp,$(TABLES))

SRC = src/Transport.cpp src/Instruments.cpp src/NasdaqMD.cpp src/Factory.cpp
TESTS = $(wildcard tests/handler/*.cpp)

OPT ?= -O2
FLAGS = $(STANDARD) $(OPT) $(DEFINES_OPT) -g -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations \
        -Wno-reorder -Iinclude -I$(GEN) -Itests/handler $(INCL) -I$(KSPRPROJ)/unit_test/include \
        -I$(GTEST_PATH)/include
KLIBS = -L$(KSPRLIB) -lframe -llogger -lmq0 -lmcast_recv -lchutil $(ACTORS_LIB) \
        -L$(BOOST_PATH)/lib -lboost_thread -lboost_filesystem -L$(ZMQ_PATH)/lib -lzmq \
        -L$(ZLIB_PATH)/lib -lz -lpthread
GTLIBS = -L$(GTEST_PATH)/lib -lgtest -lgtest_main

OBJ = $(patsubst %.cpp,build/obj/%.o,$(SRC)) $(TREE_OBJ)

# The kaspar sources whose compiled form depends on the en::x enum, compiled
# from this tree: a KSPRLIB built from a tree whose exch_code.enum predates the
# NASDAQ codes rejects them (data_pay_load::is_valid). Linked ahead of -lframe.
TREE_SRC ?= frame_kaspr/src/OB.cpp frame_kaspr/src/RefData.cpp frame_kaspr/src/point.cpp
TREE_OBJ = $(patsubst %.cpp,build/obj/tree/%.o,$(TREE_SRC))
TOBJ = $(patsubst %.cpp,build/obj/%.o,$(TESTS))

.PHONY: all headers check test replay decode_test clean
all: build/run_nasdaq_md_tests build/nasdaq_replay build/itch_filter

check:
	for t in $(TABLES); do $(PYTHON) $(FIELDGEN) $$t.tbl --check || exit 1; done

headers: $(HEADERS)

$(GEN)/%.hpp: %.tbl $(FIELDGEN)
	@mkdir -p $(GEN)
	$(PYTHON) $(FIELDGEN) $< --check
	$(PYTHON) $(FIELDGEN) $< -o $@

build/obj/%.o: %.cpp $(HEADERS) $(wildcard include/nasdaq_md/*.hpp include/nasdaq_md/*/*.hpp tests/handler/*.hpp)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

build/obj/tree/%.o: $(KSPRPROJ)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -I$(KSPRPROJ)/frame_kaspr/include -c $< -o $@

build/run_nasdaq_md_tests: $(OBJ) $(TOBJ)
	$(CXX) $^ -o $@ $(KLIBS) $(GTLIBS)

build/nasdaq_replay: $(OBJ) build/obj/tools/nasdaq_replay.o
	$(CXX) $^ -o $@ $(KLIBS)

build/itch_filter: tools/itch_filter.cpp $(HEADERS)
	$(CXX) $(STANDARD) -O2 -Wall -Wextra -I$(GEN) $< -o $@

build/nasdaq_decode_test: tests/nasdaq_decode_test.cpp $(HEADERS)
	$(CXX) -std=c++20 -Wall -Wextra -Werror -I$(GEN) $< -o $@

decode_test: build/nasdaq_decode_test
	./build/nasdaq_decode_test

replay: build/nasdaq_replay build/itch_filter

test: build/run_nasdaq_md_tests decode_test
	KSPRPROJ=$(KSPRPROJ) ./build/run_nasdaq_md_tests

clean:
	rm -rf build
