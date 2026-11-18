# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# OKX V5 order manager (not certified): OkxOM, the private-WebSocket session,
# SimOkx, tests and the demo check tool. Not part of the top-level build.
# Needs the Kaspar libraries (logger, mq0, chutil, actors), Boost (Beast),
# OpenSSL, nlohmann/json, ZeroMQ and Google Test; libframe is compiled here
# from this tree (see ../../market_data/okx/Makefile for why).
#
#   export KSPRPROJ=~/kaspar-hft
#   make test
#   make tools          # build/okx_demo_check (needs demo keys to run)
#
# Everything is built into build/, which is git-ignored.

HERE := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
KSPRPROJ ?= $(abspath $(HERE)/../../../..)
include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

KSPRLIB    ?= $(KSPRPROJ)/lib
ACTORS_LIB ?= $(KSPRPROJ)/actors/cpp/libactors.a
OPENSSL_PATH ?= /opt/homebrew/opt/openssl@3
CXX ?= c++

MD  = $(abspath $(HERE)/../../market_data/okx)
SIM = $(KSPRPROJ)/sim
B   = $(HERE)/build

FLAGS = $(STANDARD) -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wno-reorder \
        -I$(HERE)/include -I$(HERE)/tests -I$(MD)/include -I$(SIM)/include $(INCL) \
        -I$(OPENSSL_PATH)/include -I$(GTEST_PATH)/include

FRAME_SRCS = RefData.cpp Timer.cpp OB.cpp point.cpp SOM.cpp SOM_if.cpp Cons_if.cpp BFA_if.cpp \
             BinRecorder_if.cpp OB_if.cpp Timer_if.cpp QLen_if.cpp LatencyProbe_if.cpp BrokerOrderBook.cpp
FRAME_OBJ  = $(patsubst %.cpp,$(B)/obj/frame/%.o,$(FRAME_SRCS))
FRAME_LIB  = $(B)/lib/libframe.a
LIBS  = $(FRAME_LIB) -L$(KSPRLIB) -lframe -llogger -lmq0 -lchutil $(ACTORS_LIB) \
        -L$(BOOST_PATH)/lib -lboost_thread -lboost_filesystem \
        -L$(OPENSSL_PATH)/lib -lssl -lcrypto -L$(ZMQ_PATH)/lib -lzmq -L$(ZLIB_PATH)/lib -lz -lpthread
GTEST = -L$(GTEST_PATH)/lib -lgtest -lgtest_main

SRC   = src/OkxAuth.cpp src/OkxConfig.cpp src/OkxOM.cpp src/OkxWsSession.cpp src/SimOkx.cpp
TESTS = tests/test_okx_auth_config.cpp tests/test_okx_om.cpp tests/test_sim_okx.cpp

OBJ    = $(patsubst %.cpp,$(B)/obj/%.o,$(SRC)) $(B)/obj/md/OkxWs.o
HDRS   = $(wildcard $(HERE)/include/okx_oe/*.hpp $(HERE)/include/okx_oe/*/*.hpp $(HERE)/tests/*.hpp)

.PHONY: all test tools clean
all: $(B)/run_okx_oe_tests tools
tools: $(B)/okx_demo_check

$(B)/obj/%.o: $(HERE)/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

$(B)/obj/md/OkxWs.o: $(MD)/src/OkxWs.cpp $(MD)/include/okx_md/OkxWs.hpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

$(B)/obj/frame/%.o: $(KSPRPROJ)/frame_kaspr/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CFLAGS) -I$(KSPRPROJ)/frame_kaspr/include -O2 -g $(DEFINES_COMMON) $(DEFINES_OPT) -w -c $< -o $@

$(FRAME_LIB): $(FRAME_OBJ)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(B)/run_okx_oe_tests: $(patsubst %.cpp,$(B)/obj/%.o,$(TESTS)) $(OBJ) $(FRAME_LIB)
	$(CXX) $(filter %.o,$^) -o $@ $(LIBS) $(GTEST)

# The demo check also runs the post-trade client: its sources are compiled
# here from ../../post_trade/okx and ../../post_trade/common.
PT      = $(abspath $(HERE)/../../post_trade/okx)
PTC     = $(abspath $(HERE)/../../post_trade/common)
PT_SRCS = $(PT)/src/OkxFills.cpp $(PT)/src/OkxFillsClient.cpp $(PT)/src/OkxFillsWsSession.cpp $(PT)/src/SimFills.cpp \
          $(PTC)/src/TradeLedger.cpp
PT_OBJ  = $(patsubst %.cpp,$(B)/obj/pt/%.o,$(notdir $(PT_SRCS)))
PT_FLAGS = -I$(PT)/include -I$(PTC)/include

$(B)/obj/pt/%.o: $(PT)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) $(PT_FLAGS) -c $< -o $@

$(B)/obj/pt/TradeLedger.o: $(PTC)/src/TradeLedger.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) $(PT_FLAGS) -c $< -o $@

$(B)/obj/tools/okx_demo_check.o: $(HERE)/tools/okx_demo_check.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) $(PT_FLAGS) -c $< -o $@

$(B)/okx_demo_check: $(B)/obj/tools/okx_demo_check.o $(OBJ) $(PT_OBJ) $(FRAME_LIB)
	$(CXX) $(filter %.o,$^) -o $@ $(LIBS)

test: $(B)/run_okx_oe_tests
	cd $(HERE) && KSPRPROJ=$(KSPRPROJ) $(B)/run_okx_oe_tests

clean:
	rm -rf $(B)
