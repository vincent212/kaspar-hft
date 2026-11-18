# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# OKX V5 public market data (not certified): sync class, feed actor, recorder,
# live verifier and unit tests. Not part of the top-level build. Needs the
# Kaspar libraries (frame, logger, mq0, chutil, actors), Boost (Beast),
# OpenSSL, nlohmann/json, ZeroMQ and Google Test:
#
#   export KSPRPROJ=~/kaspar-hft
#   make test                      # unit tests
#   make tools                     # build/okx_record, okx_verify, okx_feed_monitor
#   make test KSPRLIB=/path/lib ACTORS_LIB=/path/actors/cpp/libactors.a
#
# Everything is built into build/, which is git-ignored.

HERE := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
KSPRPROJ ?= $(abspath $(HERE)/../../../..)
include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

KSPRLIB    ?= $(KSPRPROJ)/lib
ACTORS_LIB ?= $(KSPRPROJ)/actors/cpp/libactors.a
OPENSSL_PATH ?= /opt/homebrew/opt/openssl@3
CXX ?= c++

SIM = $(KSPRPROJ)/sim
B   = $(HERE)/build

FLAGS = $(STANDARD) -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wno-reorder \
        -I$(HERE)/include -I$(SIM)/include -I$(KSPRPROJ)/unit_test/include $(INCL) \
        -I$(OPENSSL_PATH)/include -I$(GTEST_PATH)/include
# libframe is compiled here from this tree's frame_kaspr sources: OB checks
# en::is_valid(venue) against the enum it was compiled with, and a libframe
# built before the OKX codes were added (en::x) rejects OKX and OKXF.
FRAME_SRCS = RefData.cpp Timer.cpp OB.cpp point.cpp SOM.cpp SOM_if.cpp Cons_if.cpp BFA_if.cpp \
             BinRecorder_if.cpp OB_if.cpp Timer_if.cpp QLen_if.cpp LatencyProbe_if.cpp BrokerOrderBook.cpp
FRAME_OBJ  = $(patsubst %.cpp,$(B)/obj/frame/%.o,$(FRAME_SRCS))
FRAME_LIB  = $(B)/lib/libframe.a
LIBS  = $(FRAME_LIB) -L$(KSPRLIB) -lframe -llogger -lmq0 -lchutil $(ACTORS_LIB) \
        -L$(BOOST_PATH)/lib -lboost_thread -lboost_filesystem \
        -L$(OPENSSL_PATH)/lib -lssl -lcrypto -L$(ZMQ_PATH)/lib -lzmq -L$(ZLIB_PATH)/lib -lz -lpthread
GTEST = -L$(GTEST_PATH)/lib -lgtest -lgtest_main

CORE  = src/OkxSync.cpp src/OkxWs.cpp src/OkxFeed.cpp
SIMSRC = $(SIM)/src/Mbp2Mbo.cpp $(SIM)/src/LevelWindow.cpp
TESTS = tests/test_okx_sync.cpp tests/test_okx_ob.cpp

obj = $(patsubst %.cpp,$(B)/obj/%.o,$(1))
simobj = $(patsubst $(SIM)/src/%.cpp,$(B)/obj/sim/%.o,$(1))
CORE_OBJ = $(call obj,$(CORE)) $(call simobj,$(SIMSRC))

.PHONY: all test tools recorder clean
all: tools $(B)/run_okx_md_tests
tools: $(B)/okx_record $(B)/okx_verify $(B)/okx_feed_monitor
recorder: $(B)/okx_record

$(B)/obj/%.o: $(HERE)/%.cpp $(wildcard $(HERE)/include/okx_md/*.hpp $(HERE)/include/okx_md/*/*.hpp)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

$(B)/obj/sim/%.o: $(SIM)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

$(B)/obj/frame/%.o: $(KSPRPROJ)/frame_kaspr/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CFLAGS) -I$(KSPRPROJ)/frame_kaspr/include -O2 -g $(DEFINES_COMMON) $(DEFINES_OPT) -w -c $< -o $@

$(FRAME_LIB): $(FRAME_OBJ)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(B)/okx_record: $(call obj,tools/okx_record.cpp src/OkxWs.cpp) $(FRAME_LIB)
	$(CXX) $(filter %.o,$^) -o $@ $(LIBS)

$(B)/okx_verify: $(call obj,tools/okx_verify.cpp) $(CORE_OBJ) $(FRAME_LIB)
	$(CXX) $(filter %.o,$^) -o $@ $(LIBS)

$(B)/okx_feed_monitor: $(call obj,tools/okx_feed_monitor.cpp) $(CORE_OBJ) $(FRAME_LIB)
	$(CXX) $(filter %.o,$^) -o $@ $(LIBS)

$(B)/run_okx_md_tests: $(call obj,$(TESTS)) $(CORE_OBJ) $(FRAME_LIB)
	$(CXX) $(filter %.o,$^) -o $@ $(LIBS) $(GTEST)

test: $(B)/run_okx_md_tests
	cd $(HERE) && KSPRPROJ=$(KSPRPROJ) $(B)/run_okx_md_tests

clean:
	rm -rf $(B)
