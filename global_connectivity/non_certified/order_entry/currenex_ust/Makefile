# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Currenex US Treasury OUCH 2.0 order manager (not certified): generated
# header and unit tests. Not wired into the top-level build. Needs the
# kaspar libraries (frame, logger, mq0, chutil, actors), Boost, ZeroMQ and
# Google Test:
#
#   export KSPRPROJ=~/kaspar-hft
#   make test                     # generate, build, run
#   make KSPRLIB=/path/to/lib ACTORS_LIB=/path/to/libactors.a test
#   make syntax                   # g++ -fsyntax-only on every source
#
# The message header is generated from currenex_ust_ouch.tbl into
# build/include; it is not committed.

KSPRPROJ ?= $(abspath $(CURDIR)/../../../..)
include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

KSPRLIB ?= $(KSPRPROJ)/lib
ACTORS_LIB ?= $(KSPRPROJ)/actors/cpp/libactors.a
CXX ?= c++
GXX ?= g++-15
PYTHON ?= python3
FIELDGEN = $(KSPRPROJ)/.claude/skills/build-feed-handler/tools/fieldgen.py

GEN = build/include/currenex_ust/gen
HEADERS = $(GEN)/currenex_ust_ouch.hpp

SRC = src/CurrenexOM.cpp src/CxConfig.cpp src/CxCodec.cpp src/TcpSession.cpp src/SimCurrenex.cpp
TESTS = tests/test_cx_codec.cpp tests/test_cx_om.cpp tests/test_sim_currenex.cpp tests/test_tcp_session.cpp
BOOK = $(KSPRPROJ)/frame_kaspr/src/BrokerOrderBook.cpp

FLAGS = $(STANDARD) -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wno-reorder -Iinclude -Ibuild/include -Itests $(INCL) -I$(GTEST_PATH)/include
LIBS = -L$(KSPRLIB) -lframe -llogger -lmq0 -lchutil $(ACTORS_LIB) \
       -L$(GTEST_PATH)/lib -lgtest -lgtest_main \
       -L$(BOOST_PATH)/lib -lboost_thread -lboost_filesystem -L$(ZMQ_PATH)/lib -lzmq -L$(ZLIB_PATH)/lib -lz -lpthread

OBJ = $(patsubst %.cpp,build/obj/%.o,$(SRC) $(TESTS)) build/obj/BrokerOrderBook.o

.PHONY: all headers check markdown test syntax clean
all: build/run_currenex_tests

check:
	$(PYTHON) $(FIELDGEN) currenex_ust_ouch.tbl --check

markdown:
	$(PYTHON) $(FIELDGEN) currenex_ust_ouch.tbl --markdown

headers: $(HEADERS)

$(GEN)/currenex_ust_ouch.hpp: currenex_ust_ouch.tbl $(FIELDGEN)
	@mkdir -p $(GEN)
	$(PYTHON) $(FIELDGEN) currenex_ust_ouch.tbl --check
	$(PYTHON) $(FIELDGEN) currenex_ust_ouch.tbl -o $@

build/obj/%.o: %.cpp $(HEADERS) $(wildcard include/currenex_ust/*.hpp include/currenex_ust/*/*.hpp tests/*.hpp)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

build/obj/BrokerOrderBook.o: $(BOOK)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

build/run_currenex_tests: $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LIBS)

test: build/run_currenex_tests
	KSPRPROJ=$(KSPRPROJ) ./build/run_currenex_tests

# The kaspar headers use glibc's uint and ushort, which g++ on macOS does not
# declare; build/glibc_types.h supplies them for this check only.
syntax: $(HEADERS)
	@printf 'typedef unsigned int uint;\ntypedef unsigned short ushort;\n' > build/glibc_types.h
	for f in $(SRC) $(TESTS); do $(GXX) $(FLAGS) -Wno-reorder -include build/glibc_types.h -fsyntax-only $$f || exit 1; done

clean:
	rm -rf build
