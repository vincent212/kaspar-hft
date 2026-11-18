# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Aquis Exchange market data handler (not certified): generated headers, the
# decode test and the handler's unit tests. Not wired into the top-level build.
# The handler tests need the kaspar libraries (frame, logger, mq0, chutil,
# actors), Boost, ZeroMQ and Google Test:
#
#   export KSPRPROJ=~/kaspar-hft
#   make test                  # generate, build, run both test programs
#   make KSPRLIB=/path/to/lib ACTORS_LIB=/path/to/libactors.a test
#   make decode-test           # the decode test alone: no libraries needed
#   make check                 # fieldgen --check on both tables
#
# The decoders are generated from aquis_md.tbl and aqse_md.tbl into
# build/include/aquis/gen; they are not committed.

KSPRPROJ ?= $(abspath $(CURDIR)/../../../..)
include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

KSPRLIB ?= $(KSPRPROJ)/lib
ACTORS_LIB ?= $(KSPRPROJ)/actors/cpp/libactors.a
CXX ?= c++
PYTHON ?= python3
FIELDGEN = $(KSPRPROJ)/.claude/skills/build-feed-handler/tools/fieldgen.py

GEN = build/include/aquis/gen
HEADERS = $(GEN)/aquis_md.hpp $(GEN)/aqse_md.hpp

SRC = src/Handler.cpp src/Instruments.cpp src/AquisFeed.cpp
TESTS = tests/test_handler.cpp tests/test_instruments.cpp tests/test_feed.cpp

FLAGS = $(STANDARD) -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wno-reorder \
        -Iinclude -Ibuild/include -Itests $(INCL) -I$(GTEST_PATH)/include \
        -DAQUIS_TEST_DATA='"$(CURDIR)/tests/data"'
LIBS = -L$(KSPRLIB) -lframe -llogger -lmq0 -lchutil $(ACTORS_LIB) \
       -L$(GTEST_PATH)/lib -lgtest -lgtest_main \
       -L$(BOOST_PATH)/lib -lboost_thread -lboost_filesystem -L$(ZMQ_PATH)/lib -lzmq -L$(ZLIB_PATH)/lib -lz -lpthread

OBJ = $(patsubst %.cpp,build/obj/%.o,$(SRC) $(TESTS))

.PHONY: all headers check test decode-test clean
all: build/run_aquis_tests build/aquis_decode_test

check:
	$(PYTHON) $(FIELDGEN) aquis_md.tbl --check
	$(PYTHON) $(FIELDGEN) aqse_md.tbl --check

headers: $(HEADERS)

$(GEN)/%.hpp: %.tbl $(FIELDGEN)
	@mkdir -p $(GEN)
	$(PYTHON) $(FIELDGEN) $< --check
	$(PYTHON) $(FIELDGEN) $< -o $@

build/obj/%.o: %.cpp $(HEADERS) $(wildcard include/aquis/*.hpp include/aquis/*/*.hpp tests/*.hpp)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

build/run_aquis_tests: $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LIBS)

build/aquis_decode_test: tests/aquis_decode_test.cpp $(HEADERS)
	$(CXX) -std=c++20 -Wall -Wextra -Werror -I$(GEN) $< -o $@

decode-test: build/aquis_decode_test
	./build/aquis_decode_test

test: build/run_aquis_tests build/aquis_decode_test
	./build/aquis_decode_test
	KSPRPROJ=$(KSPRPROJ) ./build/run_aquis_tests

clean:
	rm -rf build
