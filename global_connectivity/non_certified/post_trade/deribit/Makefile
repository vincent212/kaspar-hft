# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Deribit post-trade client (not certified): trades into post_trade/common's
# TradeLedger, with REST reconciliation, SimFills and tests. Not part of the
# top-level build. Uses ../../order_entry/deribit (WebSocket client, signing,
# environment guard, configuration).
#
#   export KSPRPROJ=~/kaspar-hft
#   make test
#   make KSPRLIB=/path/to/lib ACTORS_LIB=/path/to/libactors.a test
#
# Everything is built into build/, which is git-ignored.

HERE := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
KSPRPROJ ?= $(abspath $(HERE)/../../../..)
include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

KSPRLIB    ?= $(KSPRPROJ)/lib
ACTORS_LIB ?= $(KSPRPROJ)/actors/cpp/libactors.a
OPENSSL_PATH ?= /opt/homebrew/opt/openssl@3
CXX ?= c++

OE     = $(abspath $(HERE)/../../order_entry/deribit)
COMMON = $(abspath $(HERE)/../common)
B      = $(HERE)/build

FLAGS = $(STANDARD) -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wno-reorder \
        -I$(HERE)/include -I$(COMMON)/include -I$(OE)/include -I$(KSPRPROJ)/sim/include $(INCL) \
        -I$(OPENSSL_PATH)/include -I$(GTEST_PATH)/include -DDERIBIT_OE_CONFIG=\"$(OE)/config/deribit_test_example.ini\"

FRAME_SRCS = RefData.cpp Timer.cpp OB.cpp point.cpp SOM.cpp SOM_if.cpp Cons_if.cpp BFA_if.cpp \
             BinRecorder_if.cpp OB_if.cpp Timer_if.cpp QLen_if.cpp LatencyProbe_if.cpp BrokerOrderBook.cpp
FRAME_OBJ  = $(patsubst %.cpp,$(B)/obj/frame/%.o,$(FRAME_SRCS))
FRAME_LIB  = $(B)/lib/libframe.a
LIBS  = $(FRAME_LIB) -L$(KSPRLIB) -lframe -llogger -lmq0 -lchutil $(ACTORS_LIB) \
        -L$(BOOST_PATH)/lib -lboost_thread -lboost_filesystem \
        -L$(OPENSSL_PATH)/lib -lssl -lcrypto -L$(ZMQ_PATH)/lib -lzmq -L$(ZLIB_PATH)/lib -lz -lpthread
GTEST = -L$(GTEST_PATH)/lib -lgtest -lgtest_main

SRC   = src/DeribitFills.cpp src/DeribitFillsClient.cpp src/DeribitFillsWsSession.cpp src/SimFills.cpp
TESTS = tests/test_deribit_fills.cpp
EXT   = $(B)/obj/ext/RpcWs.o $(B)/obj/ext/Signing.o $(B)/obj/ext/DeribitConfig.o $(B)/obj/ext/TradeLedger.o
OBJ   = $(patsubst %.cpp,$(B)/obj/%.o,$(SRC)) $(EXT)
HDRS  = $(wildcard $(HERE)/include/deribit_pt/*.hpp $(HERE)/include/deribit_pt/*/*.hpp)

.PHONY: all test clean
all: $(B)/run_deribit_pt_tests

$(B)/obj/%.o: $(HERE)/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

$(B)/obj/ext/%.o: $(OE)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@
$(B)/obj/ext/TradeLedger.o: $(COMMON)/src/TradeLedger.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

$(B)/obj/frame/%.o: $(KSPRPROJ)/frame_kaspr/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CFLAGS) -I$(KSPRPROJ)/frame_kaspr/include -O2 -g $(DEFINES_COMMON) $(DEFINES_OPT) -w -c $< -o $@

$(FRAME_LIB): $(FRAME_OBJ)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(B)/run_deribit_pt_tests: $(patsubst %.cpp,$(B)/obj/%.o,$(TESTS)) $(OBJ) $(FRAME_LIB)
	$(CXX) $(filter %.o,$^) -o $@ $(LIBS) $(GTEST)

test: $(B)/run_deribit_pt_tests
	cd $(HERE) && KSPRPROJ=$(KSPRPROJ) $(B)/run_deribit_pt_tests

clean:
	rm -rf $(B)
