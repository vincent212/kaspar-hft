# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Japannext PTS equities market data (not certified): ITCH over MoldUDP64 with
# GLIMPSE recovery. Not wired into the top-level build. Needs the kaspar
# libraries (frame, logger, mq0, chutil, mcast_recv, actors), Boost, ZeroMQ,
# zlib and Google Test:
#
#   export KSPRPROJ=~/kaspar-hft
#   make test                         # generate, build, run the unit tests
#   make KSPRLIB=/path/to/lib ACTORS_LIB=/path/to/libactors.a test
#   make replay JNX_DATA=/dir/with/the/sample/pcaps
#
# The decoders are generated from itch.tbl, moldudp64.tbl and glimpse.tbl into
# build/include/jnx_md/gen; they are not committed.

KSPRPROJ ?= $(abspath $(CURDIR)/../../../..)
include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

KSPRLIB ?= $(KSPRPROJ)/lib
ACTORS_LIB ?= $(KSPRPROJ)/actors/cpp/libactors.a
CXX ?= c++
PYTHON ?= python3
FIELDGEN = $(KSPRPROJ)/.claude/skills/build-feed-handler/tools/fieldgen.py
JNX_DATA ?=

GEN = build/include/jnx_md/gen
HEADERS = $(GEN)/itch.hpp $(GEN)/moldudp64.hpp $(GEN)/glimpse.hpp

LIBSRC = src/Instruments.cpp src/Dispatcher.cpp src/Handler.cpp src/Pcap.cpp src/Writer.cpp \
         src/JnxFeed.cpp src/GlimpseClient.cpp src/JnxFactory.cpp
TESTS = tests/test_dispatcher.cpp tests/test_handler.cpp tests/test_glimpse.cpp \
        tests/test_ob.cpp
TOOLS = tools/jnx_replay.cpp tools/BookCheck.cpp

# DEFINES_OPT as the kaspar libraries are built, so that the OB and OrderQ
# headers compile here as they did there.
FLAGS = $(STANDARD) -O2 -g $(DEFINES_COMMON) $(DEFINES_OPT) -Wall -Wextra -Wno-unused-parameter \
        -Wno-deprecated-declarations -Wno-reorder -Wno-sign-compare \
        -Iinclude -Ibuild/include -Itools $(INCL) -I$(GTEST_PATH)/include
LIBS = -L$(KSPRLIB) -lframe -lmcast_recv -llogger -lmq0 -lchutil $(ACTORS_LIB) \
       -L$(BOOST_PATH)/lib -lboost_thread -lboost_filesystem -L$(ZMQ_PATH)/lib -lzmq \
       -L$(ZLIB_PATH)/lib -lz -lpthread
GTEST = -L$(GTEST_PATH)/lib -lgtest -lgtest_main

# OB and RefData are compiled from this tree rather than taken from libframe: a
# libframe built before JNX was added to en::x (exch_code.enum) rejects JNX
# records (Data.hpp: ASSERT en::is_valid(mkt)). Linked ahead of -lframe, these
# objects take the place of libframe's.
KASPAR_SRC = OB.cpp RefData.cpp point.cpp
KASPAR_OBJ = $(patsubst %.cpp,build/obj/kaspar/%.o,$(KASPAR_SRC))

LIBOBJ = $(patsubst %.cpp,build/obj/%.o,$(LIBSRC))
TESTOBJ = $(patsubst %.cpp,build/obj/%.o,$(TESTS)) build/obj/tools/BookCheck.o
DEPS = $(HEADERS) $(wildcard include/jnx_md/*.hpp include/jnx_md/*/*.hpp tools/*.hpp)

.PHONY: all headers check test replay clean
all: build/libjnx_md.a build/run_jnx_tests build/jnx_replay build/jnx_decode_test

check:
	for t in itch moldudp64 glimpse; do $(PYTHON) $(FIELDGEN) $$t.tbl --check || exit 1; done

headers: $(HEADERS)

$(GEN)/%.hpp: %.tbl $(FIELDGEN)
	@mkdir -p $(GEN)
	$(PYTHON) $(FIELDGEN) $< --check
	$(PYTHON) $(FIELDGEN) $< -o $@

build/obj/%.o: %.cpp $(DEPS)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

build/obj/kaspar/%.o: $(KSPRPROJ)/frame_kaspr/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

build/libjnx_md.a: $(LIBOBJ)
	ar rcs $@ $^

build/run_jnx_tests: $(TESTOBJ) build/libjnx_md.a $(KASPAR_OBJ)
	$(CXX) $(TESTOBJ) build/libjnx_md.a $(KASPAR_OBJ) -o $@ $(LIBS) $(GTEST)

build/jnx_replay: build/obj/tools/jnx_replay.o build/obj/tools/BookCheck.o build/libjnx_md.a $(KASPAR_OBJ)
	$(CXX) $^ -o $@ $(LIBS)

# The venue's decode test, against the generated headers alone.
build/jnx_decode_test: tests/jnx_decode_test.cpp $(HEADERS)
	$(CXX) -std=c++20 -Wall -Wextra -Werror -I$(GEN) $< -o $@

test: build/run_jnx_tests build/jnx_decode_test
	./build/jnx_decode_test
	JNX_DATA=$(JNX_DATA) KSPRPROJ=$(KSPRPROJ) ./build/run_jnx_tests

replay: build/jnx_replay
	./build/jnx_replay --udp $(JNX_DATA)/Japannext_PTS_ITCH_Equities_v1.7.UDP.pcap \
	  --glimpse $(JNX_DATA)/Japannext_PTS_GLIMPSE_Equities_v1.4.pcap --all

clean:
	rm -rf build
