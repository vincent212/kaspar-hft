# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Bybit V5 public market data (not certified): BybitSync, the BybitFeed actor,
# the recorder, the live verify tool, the archive replay tool and unit tests.
# Not wired into the top-level build. Needs the kaspar libraries (frame,
# logger, mq0, chutil, actors), Boost (Beast), OpenSSL, nlohmann/json, ZeroMQ
# and Google Test:
#
#   export KSPRPROJ=~/kaspar-hft
#   make test                 # unit tests (recorded frames, documentation examples)
#   make tools                # build/bybit_verify, build/bybit_record, build/bybit_replay
#   make KSPRLIB=/path/to/lib ACTORS_LIB=/path/to/libactors.a test
#
# Mbp2Mbo and LevelWindow are compiled from $(KSPRPROJ)/sim/src.

KSPRPROJ ?= $(abspath $(CURDIR)/../../../..)
include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

KSPRLIB ?= $(KSPRPROJ)/lib
ACTORS_LIB ?= $(KSPRPROJ)/actors/cpp/libactors.a
OPENSSL_PATH ?= /opt/homebrew/opt/openssl@3
CXX ?= c++

SIM = $(KSPRPROJ)/sim/src
SRC = src/BybitSync.cpp src/BybitFeed.cpp
SIMSRC = $(SIM)/Mbp2Mbo.cpp $(SIM)/LevelWindow.cpp
TESTS = tests/test_bybit_sync.cpp tests/test_bybit_chain_ob.cpp tests/test_bybit_feed.cpp

FLAGS = $(STANDARD) -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations \
        -Wno-reorder -Wno-sign-compare -Iinclude -Itools -I$(KSPRPROJ)/sim/include \
        -I$(KSPRPROJ)/unit_test/include $(INCL) -I$(GTEST_PATH)/include -I$(OPENSSL_PATH)/include
# libframe is compiled here from this checkout's frame_kaspr sources: a
# libframe built before the BYBIT and BYBITF venue codes existed rejects them
# (en::is_valid, inlined into OB). LOCAL_FRAME=0 links $(KSPRLIB)/libframe.a.
LOCAL_FRAME ?= 1
FRAME_SRC = $(addprefix $(KSPRPROJ)/frame_kaspr/src/,RefData.cpp Timer.cpp OB.cpp point.cpp SOM.cpp \
            SOM_if.cpp Cons_if.cpp BFA_if.cpp BinRecorder_if.cpp OB_if.cpp Timer_if.cpp QLen_if.cpp \
            LatencyProbe_if.cpp BrokerOrderBook.cpp)
ifeq ($(LOCAL_FRAME),1)
FRAME_LIB = build/kl/libframe_local.a
else
FRAME_LIB = -lframe
endif
LIBS = -L$(KSPRLIB) $(FRAME_LIB) -llogger -lmq0 -lchutil $(ACTORS_LIB) \
       -L$(BOOST_PATH)/lib -lboost_thread -lboost_filesystem -L$(ZMQ_PATH)/lib -lzmq \
       -L$(ZLIB_PATH)/lib -lz -L$(OPENSSL_PATH)/lib -lssl -lcrypto -lpthread
GTEST_LIBS = -L$(GTEST_PATH)/lib -lgtest -lgtest_main

obj = $(patsubst %.cpp,build/obj/%.o,$(notdir $(1)))
LIBOBJ = $(call obj,$(SRC) $(SIMSRC))
HDRS = $(wildcard include/bybit_md/*.hpp include/bybit_md/*/*.hpp tools/*.hpp)

.PHONY: all test tools clean
all: build/run_bybit_md_tests tools
tools: build/bybit_verify build/bybit_record build/bybit_replay

build/obj/%.o: src/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@
build/obj/%.o: $(SIM)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@
build/obj/%.o: tests/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -DBYBIT_TEST_DATA=\"$(CURDIR)/tests/data\" -c $< -o $@
build/obj/%.o: tools/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

build/kl/obj/%.o: $(KSPRPROJ)/frame_kaspr/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CFLAGS) $(DEFINES_COMMON) -I$(KSPRPROJ)/frame_kaspr/include -O2 -g -w -c $< -o $@
build/kl/libframe_local.a: $(patsubst %.cpp,build/kl/obj/%.o,$(notdir $(FRAME_SRC)))
	ar rcs $@ $^

ifeq ($(LOCAL_FRAME),1)
LINKDEPS = build/kl/libframe_local.a
endif

build/run_bybit_md_tests: $(LIBOBJ) $(call obj,$(TESTS)) $(LINKDEPS)
	$(CXX) $(LIBOBJ) $(call obj,$(TESTS)) -o $@ $(GTEST_LIBS) $(LIBS)

build/bybit_verify: $(LIBOBJ) build/obj/bybit_verify.o $(LINKDEPS)
	$(CXX) $(LIBOBJ) build/obj/bybit_verify.o -o $@ $(LIBS)
build/bybit_record: $(LIBOBJ) build/obj/bybit_record.o $(LINKDEPS)
	$(CXX) $(LIBOBJ) build/obj/bybit_record.o -o $@ $(LIBS)
build/bybit_replay: $(LIBOBJ) build/obj/bybit_replay.o $(LINKDEPS)
	$(CXX) $(LIBOBJ) build/obj/bybit_replay.o -o $@ $(LIBS)

test: build/run_bybit_md_tests
	KSPRPROJ=$(KSPRPROJ) ./build/run_bybit_md_tests

clean:
	rm -rf build
